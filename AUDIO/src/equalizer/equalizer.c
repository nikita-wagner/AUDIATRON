#include "equalizer/equalizer.h"

#define EQ_GLIDE_SECONDS 0.010f    // how long a retuned band takes to its new coefficients
#define EQ_GLIDE_STEP    16         // frames between two coefficient steps of a glide

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
        eq->bands[b].target = eq->bands[b].coeffs;
    }
}

void eq_reset(AT_Equalizer* eq) {
    for (int b = 0; b < AT_EQ_BANDS; b++) reset_band(&eq->bands[b]);
}

void eq_set_band(AT_Equalizer* eq, uint32_t band, AT_FilterType type,
                 float freq, float q, float gain_db) {
    if (band >= AT_EQ_BANDS || type >= AT_FILTER_COUNT) return;
    AT_EQBand* b = &eq->bands[band];

    // Switching a band on starts it from clean memory with its coefficients at
    // once; retuning an active band keeps its state and glides the coefficients
    // to the new ones over EQ_GLIDE_SECONDS, so sweeps neither pop nor zip.
    const bool fresh = b->type == AT_FILTER_OFF || type == AT_FILTER_OFF;
    if (fresh) reset_band(b);

    b->type    = type;
    b->freq    = freq;
    b->q       = q;
    b->gain_db = gain_db;
    b->target  = biquad_design(type, eq->sample_rate, freq, q, gain_db);
    b->glide   = fresh ? 0 : (uint32_t)(eq->sample_rate * EQ_GLIDE_SECONDS);
    if (b->glide == 0) b->coeffs = b->target;
}

// The coefficients a step of n frames closer to the target (linear in time: they arrive when the glide ends).
static void glide_step(AT_EQBand* b, uint32_t n) {
    const float t = n >= b->glide ? 1.0f : (float)n / (float)b->glide;
    AT_BiquadCoeffs* c = &b->coeffs;
    const AT_BiquadCoeffs* g = &b->target;
    c->b0 += (g->b0 - c->b0) * t;
    c->b1 += (g->b1 - c->b1) * t;
    c->b2 += (g->b2 - c->b2) * t;
    c->a1 += (g->a1 - c->a1) * t;
    c->a2 += (g->a2 - c->a2) * t;
    b->glide = n >= b->glide ? 0 : b->glide - n;
}

void eq_process(AT_Equalizer* eq, AT_Buffer* buf) {
    if (eq->bypass) return;

    for (int n = 0; n < AT_EQ_BANDS; n++) {
        AT_EQBand* b = &eq->bands[n];
        if (b->type == AT_FILTER_OFF) continue;

        if (b->glide == 0) {
            for (uint32_t c = 0; c < buf->channels && c < AT_MAX_CHANNELS; c++)
                biquad_process(&b->coeffs, &b->state[c], buf->ch[c], buf->frames);
            continue;
        }
        // gliding: in short steps, each with coefficients a little closer
        for (uint32_t done = 0; done < buf->frames; ) {
            uint32_t step = buf->frames - done;
            if (step > EQ_GLIDE_STEP) step = EQ_GLIDE_STEP;
            if (b->glide) glide_step(b, step);
            for (uint32_t c = 0; c < buf->channels && c < AT_MAX_CHANNELS; c++)
                biquad_process(&b->coeffs, &b->state[c], buf->ch[c] + done, step);
            done += step;
        }
    }
}
