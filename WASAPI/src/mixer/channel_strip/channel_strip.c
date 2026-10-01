#include "mixer/channel_strip/channel_strip.h"

#include "core/at_math.h"

#define SMOOTH_SECONDS 0.010f

void channel_strip_init(AT_ChannelStrip* s, float sample_rate) {
    s->volume      = 0.8f;
    s->pan         = 0.0f;
    s->mute        = false;
    s->gain_l      = s->volume;     // start settled — no fade-in on the first block
    s->gain_r      = s->volume;
    s->smooth_coef = at_smoothing_coef(SMOOTH_SECONDS, sample_rate);
    s->peak        = 0.0f;
}

void channel_strip_mix(AT_ChannelStrip* s, const AT_Buffer* in, AT_Buffer* bus) {
    float vol = s->mute ? 0.0f : s->volume;
    float tl  = vol * (s->pan > 0.0f ? 1.0f - s->pan : 1.0f);
    float tr  = vol * (s->pan < 0.0f ? 1.0f + s->pan : 1.0f);
    float gl  = s->gain_l, gr = s->gain_r;
    float k   = s->smooth_coef;

    // silent and settled → skip the whole strip
    if (tl == 0.0f && tr == 0.0f && gl < 1e-6f && gr < 1e-6f) {
        s->gain_l = s->gain_r = 0.0f;
        s->peak   = 0.0f;
        return;
    }

    const float* in_l  = in->ch[0];
    const float* in_r  = in->channels > 1 ? in->ch[1] : in->ch[0];
    float*       out_l = bus->ch[0];
    float*       out_r = bus->ch[1];
    float        peak  = 0.0f;

    for (uint32_t i = 0; i < in->frames; i++) {
        gl += (tl - gl) * k;
        gr += (tr - gr) * k;
        float l = in_l[i] * gl;
        float r = in_r[i] * gr;
        out_l[i] += l;
        out_r[i] += r;
        peak = fmaxf(peak, fmaxf(fabsf(l), fabsf(r)));
    }

    s->gain_l = gl;
    s->gain_r = gr;
    s->peak   = peak;
}
