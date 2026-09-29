#include "mixer.h"

#define SMOOTH_SECONDS  0.010f
#define SOFTCLIP_KNEE   0.8f        // linear below this, curved above

void mixer_init(AT_Mixer* m, float sample_rate) {
    memset(m, 0, sizeof *m);
    m->sample_rate   = sample_rate;
    m->smooth_coef   = 1.0f - expf(-1.0f / (SMOOTH_SECONDS * sample_rate));
    m->master_volume = 0.8f;
    m->master_gain   = m->master_volume;
    m->soft_clip     = true;
    eq_init(&m->master_eq, sample_rate);

    for (int i = 0; i < AT_MAX_MIX_CHANNELS; i++) {
        AT_MixerChannel* c = &m->ch[i];
        c->volume = 0.8f;
        c->gain_l = c->volume;      // start settled — no fade-in on the first block
        c->gain_r = c->volume;
        eq_init(&c->eq, sample_rate);
    }
}

void mixer_channel_to_bus(AT_Mixer* m, uint32_t index, const AT_Buffer* strip, AT_Buffer* bus) {
    if (index >= AT_MAX_MIX_CHANNELS) return;
    AT_MixerChannel* c = &m->ch[index];

    float vol = c->mute ? 0.0f : c->volume;
    float tl  = vol * (c->pan > 0.0f ? 1.0f - c->pan : 1.0f);
    float tr  = vol * (c->pan < 0.0f ? 1.0f + c->pan : 1.0f);
    float gl  = c->gain_l, gr = c->gain_r;
    float k   = m->smooth_coef;

    // silent and settled → skip the whole strip
    if (tl == 0.0f && tr == 0.0f && gl < 1e-6f && gr < 1e-6f) {
        c->gain_l = c->gain_r = 0.0f;
        c->peak   = 0.0f;
        return;
    }

    const float* in_l = strip->ch[0];
    const float* in_r = strip->channels > 1 ? strip->ch[1] : strip->ch[0];
    float*       out_l = bus->ch[0];
    float*       out_r = bus->ch[1];
    float        peak  = 0.0f;

    for (uint32_t i = 0; i < strip->frames; i++) {
        gl += (tl - gl) * k;
        gr += (tr - gr) * k;
        float l = in_l[i] * gl;
        float r = in_r[i] * gr;
        out_l[i] += l;
        out_r[i] += r;
        peak = fmaxf(peak, fmaxf(fabsf(l), fabsf(r)));
    }

    c->gain_l = gl;
    c->gain_r = gr;
    c->peak   = peak;
}

void mixer_master_gain(AT_Mixer* m, AT_Buffer* bus) {
    float g = m->master_gain, t = m->master_volume, k = m->smooth_coef;
    for (uint32_t i = 0; i < bus->frames; i++) {
        g += (t - g) * k;
        for (uint32_t c = 0; c < bus->channels; c++)
            bus->ch[c][i] *= g;
    }
    m->master_gain = g;
}

static inline float soft_clip(float x) {
    float a = fabsf(x);
    if (a <= SOFTCLIP_KNEE) return x;
    float over = (a - SOFTCLIP_KNEE) / (1.0f - SOFTCLIP_KNEE);
    float y    = SOFTCLIP_KNEE + (1.0f - SOFTCLIP_KNEE) * tanhf(over);
    return x < 0.0f ? -y : y;
}

void mixer_master_limit(AT_Mixer* m, AT_Buffer* bus) {
    for (uint32_t c = 0; c < bus->channels && c < AT_MAX_CHANNELS; c++) {
        float* x    = bus->ch[c];
        float  peak = 0.0f;
        for (uint32_t i = 0; i < bus->frames; i++) {
            float y = m->soft_clip ? soft_clip(x[i]) : at_clampf(x[i], -1.0f, 1.0f);
            x[i] = y;
            peak = fmaxf(peak, fabsf(y));
        }
        m->master_peak[c] = peak;
    }
}
