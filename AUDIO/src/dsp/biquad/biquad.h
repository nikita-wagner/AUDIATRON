// ═══════════════════════════════════════════════════════════════════════════
//  dsp/biquad/biquad.h — one 2nd-order IIR filter (RBJ "Audio EQ Cookbook")
//
//  A building block: coefficients (what the filter does) and state (its
//  memory) are separate, so one coefficient set can drive many channels.
//  Runs in Transposed Direct Form II — 2 state floats per channel, good
//  float behavior.
//
//  Reference: Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad
//  filter coefficients" — the formulas in biquad.c are taken 1:1 from it.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdint.h>

typedef enum {
    AT_FILTER_OFF,
    AT_FILTER_LOWPASS,
    AT_FILTER_HIGHPASS,
    AT_FILTER_BANDPASS,
    AT_FILTER_NOTCH,
    AT_FILTER_PEAK,         // bell: gain_db at freq, width from q
    AT_FILTER_LOWSHELF,
    AT_FILTER_HIGHSHELF,
    AT_FILTER_COUNT
} AT_FilterType;

typedef struct {
    float b0, b1, b2, a1, a2;       // normalized (a0 = 1)
} AT_BiquadCoeffs;

typedef struct {
    float z1, z2;
} AT_BiquadState;

const char* biquad_filter_name(AT_FilterType t);

// Pure function — also handy for drawing a frequency response in a UI later.
//   q       : 0.707 = Butterworth; higher = narrower
//   gain_db : PEAK / SHELF only
AT_BiquadCoeffs biquad_design(AT_FilterType type, float sample_rate,
                              float freq, float q, float gain_db);

static inline void biquad_reset(AT_BiquadState* s) { s->z1 = 0.0f; s->z2 = 0.0f; }

// Filters one channel in place.
void biquad_process(const AT_BiquadCoeffs* c, AT_BiquadState* s, float* x, uint32_t frames);
