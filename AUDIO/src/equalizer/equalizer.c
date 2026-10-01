#include "equalizer/equalizer.h"

static void reset_band(AT_EQBand* b) {
    for (int c = 0; c < AT_MAX_CHANNELS; c++) biquad_reset(&b->state[c]);
}

void eq_init(AT_Equalizer* eq, float sample_rate) {
    memset(eq, 0, sizeof *eq);
    eq->sample_rate = sample_rate;
    for (int b = 0; b < AT_EQ_BANDS; b++) {
        eq->bands[b].type   = AT_FILTER_OFF;
        eq->bands[b].freq   = 1000.0f;
        eq->bands[b].q      = 0.707f;
        eq->bands[b].coeffs = (AT_BiquadCoeffs){ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }
}

void eq_reset(AT_Equalizer* eq) {
    for (int b = 0; b < AT_EQ_BANDS; b++) reset_band(&eq->bands[b]);
}

void eq_set_band(AT_Equalizer* eq, uint32_t band, AT_FilterType type,
                 float freq, float q, float gain_db) {
    if (band >= AT_EQ_BANDS || type >= AT_FILTER_COUNT) return;
    AT_EQBand* b = &eq->bands[band];

    // Switching a band on starts it from clean memory; retuning an active
    // band keeps its state so parameter sweeps don't pop.
    // TODO: coefficient smoothing for fast sweeps (interpolate b/a per block).
    if (b->type == AT_FILTER_OFF || type == AT_FILTER_OFF) reset_band(b);

    b->type    = type;
    b->freq    = freq;
    b->q       = q;
    b->gain_db = gain_db;
    b->coeffs  = biquad_design(type, eq->sample_rate, freq, q, gain_db);
}

void eq_process(AT_Equalizer* eq, AT_Buffer* buf) {
    if (eq->bypass) return;

    for (int n = 0; n < AT_EQ_BANDS; n++) {
        AT_EQBand* b = &eq->bands[n];
        if (b->type == AT_FILTER_OFF) continue;

        for (uint32_t c = 0; c < buf->channels && c < AT_MAX_CHANNELS; c++)
            biquad_process(&b->coeffs, &b->state[c], buf->ch[c], buf->frames);
    }
}
