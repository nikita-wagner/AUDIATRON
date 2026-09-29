#include "equalizer.h"

const char* eq_filter_name(AT_FilterType t) {
    switch (t) {
    case AT_FILTER_OFF:       return "off";
    case AT_FILTER_LOWPASS:   return "lowpass";
    case AT_FILTER_HIGHPASS:  return "highpass";
    case AT_FILTER_BANDPASS:  return "bandpass";
    case AT_FILTER_NOTCH:     return "notch";
    case AT_FILTER_PEAK:      return "peak";
    case AT_FILTER_LOWSHELF:  return "lowshelf";
    case AT_FILTER_HIGHSHELF: return "highshelf";
    default:                  return "?";
    }
}

void eq_init(AT_Equalizer* eq, float sample_rate) {
    memset(eq, 0, sizeof *eq);
    eq->sample_rate = sample_rate;
    for (int b = 0; b < AT_MAX_EQ_BANDS; b++) {
        eq->bands[b].type = AT_FILTER_OFF;
        eq->bands[b].freq = 1000.0f;
        eq->bands[b].q    = 0.707f;
        eq->bands[b].c    = (AT_BiquadCoeffs){ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }
}

void eq_reset(AT_Equalizer* eq) {
    for (int b = 0; b < AT_MAX_EQ_BANDS; b++) {
        memset(eq->bands[b].z1, 0, sizeof eq->bands[b].z1);
        memset(eq->bands[b].z2, 0, sizeof eq->bands[b].z2);
    }
}

AT_BiquadCoeffs eq_design(AT_FilterType type, float sample_rate,
                          float freq, float q, float gain_db) {
    // keep the math well-defined: 10 Hz .. just below Nyquist, q > 0
    freq = at_clampf(freq, 10.0f, sample_rate * 0.49f);
    q    = fmaxf(q, 0.05f);

    double A     = pow(10.0, gain_db / 40.0);
    double w0    = AT_TWO_PI * freq / sample_rate;
    double cw    = cos(w0);
    double sw    = sin(w0);
    double alpha = sw / (2.0 * q);
    double sqA2a = 2.0 * sqrt(A) * alpha;

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type) {
    case AT_FILTER_LOWPASS:
        b0 = (1 - cw) / 2;  b1 = 1 - cw;     b2 = (1 - cw) / 2;
        a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
        break;
    case AT_FILTER_HIGHPASS:
        b0 = (1 + cw) / 2;  b1 = -(1 + cw);  b2 = (1 + cw) / 2;
        a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
        break;
    case AT_FILTER_BANDPASS:                 // constant 0 dB peak gain
        b0 = alpha;         b1 = 0;          b2 = -alpha;
        a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
        break;
    case AT_FILTER_NOTCH:
        b0 = 1;             b1 = -2 * cw;    b2 = 1;
        a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
        break;
    case AT_FILTER_PEAK:
        b0 = 1 + alpha * A; b1 = -2 * cw;    b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw;    a2 = 1 - alpha / A;
        break;
    case AT_FILTER_LOWSHELF:
        b0 =      A * ((A + 1) - (A - 1) * cw + sqA2a);
        b1 =  2 * A * ((A - 1) - (A + 1) * cw);
        b2 =      A * ((A + 1) - (A - 1) * cw - sqA2a);
        a0 =           (A + 1) + (A - 1) * cw + sqA2a;
        a1 =     -2 * ((A - 1) + (A + 1) * cw);
        a2 =           (A + 1) + (A - 1) * cw - sqA2a;
        break;
    case AT_FILTER_HIGHSHELF:
        b0 =      A * ((A + 1) + (A - 1) * cw + sqA2a);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 =      A * ((A + 1) + (A - 1) * cw - sqA2a);
        a0 =           (A + 1) - (A - 1) * cw + sqA2a;
        a1 =      2 * ((A - 1) - (A + 1) * cw);
        a2 =           (A + 1) - (A - 1) * cw - sqA2a;
        break;
    default:
        break;                               // OFF → identity
    }

    AT_BiquadCoeffs c = {
        (float)(b0 / a0), (float)(b1 / a0), (float)(b2 / a0),
        (float)(a1 / a0), (float)(a2 / a0)
    };
    return c;
}

void eq_set_band(AT_Equalizer* eq, uint32_t band, AT_FilterType type,
                 float freq, float q, float gain_db) {
    if (band >= AT_MAX_EQ_BANDS || type >= AT_FILTER_COUNT) return;
    AT_EQBand* b = &eq->bands[band];

    // Switching a band on starts it from clean memory; retuning an active
    // band keeps its state so parameter sweeps don't pop.
    // TODO: coefficient smoothing for fast sweeps (interpolate b/a per block).
    if (b->type == AT_FILTER_OFF || type == AT_FILTER_OFF) {
        memset(b->z1, 0, sizeof b->z1);
        memset(b->z2, 0, sizeof b->z2);
    }

    b->type    = type;
    b->freq    = freq;
    b->q       = q;
    b->gain_db = gain_db;
    b->c       = eq_design(type, eq->sample_rate, freq, q, gain_db);
}

// Transposed Direct Form II:
//   y  = b0*x + z1
//   z1 = b1*x - a1*y + z2
//   z2 = b2*x - a2*y
void eq_process(AT_Equalizer* eq, AT_Buffer* buf) {
    if (eq->bypass) return;

    for (int n = 0; n < AT_MAX_EQ_BANDS; n++) {
        AT_EQBand* b = &eq->bands[n];
        if (b->type == AT_FILTER_OFF) continue;

        const AT_BiquadCoeffs c = b->c;
        for (uint32_t ch = 0; ch < buf->channels && ch < AT_MAX_CHANNELS; ch++) {
            float* x  = buf->ch[ch];
            float  z1 = b->z1[ch];
            float  z2 = b->z2[ch];
            for (uint32_t i = 0; i < buf->frames; i++) {
                float in = x[i];
                float y  = c.b0 * in + z1;
                z1 = c.b1 * in - c.a1 * y + z2;
                z2 = c.b2 * in - c.a2 * y;
                x[i] = y;
            }
            b->z1[ch] = z1;
            b->z2[ch] = z2;
        }
    }
}
