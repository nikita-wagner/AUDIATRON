// ═══════════════════════════════════════════════════════════════════════════
//  equalizer.h — parametric EQ built from RBJ "Audio EQ Cookbook" biquads
//
//  Each band is a 2nd-order IIR filter in Transposed Direct Form II
//  (2 state floats per channel, good float behavior). An equalizer is a
//  serial chain of up to AT_MAX_EQ_BANDS bands; bands of type AT_FILTER_OFF
//  cost nothing.
//
//  Reference: Robert Bristow-Johnson, "Cookbook formulae for audio EQ biquad
//  filter coefficients" — the formulas below are taken 1:1 from it.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "at_common.h"

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
    AT_FilterType   type;
    float           freq;           // Hz
    float           q;              // 0.707 = Butterworth; higher = narrower
    float           gain_db;        // PEAK / SHELF only
    AT_BiquadCoeffs c;
    float           z1[AT_MAX_CHANNELS];
    float           z2[AT_MAX_CHANNELS];
} AT_EQBand;

typedef struct {
    AT_EQBand bands[AT_MAX_EQ_BANDS];
    bool      bypass;
    float     sample_rate;
} AT_Equalizer;

const char* eq_filter_name(AT_FilterType t);

void eq_init    (AT_Equalizer* eq, float sample_rate);
void eq_reset   (AT_Equalizer* eq);                     // clear filter memory
void eq_set_band(AT_Equalizer* eq, uint32_t band, AT_FilterType type,
                 float freq, float q, float gain_db);

// In place, all channels of buf.
void eq_process(AT_Equalizer* eq, AT_Buffer* buf);

// Pure function — also handy for drawing the frequency response in a UI later.
AT_BiquadCoeffs eq_design(AT_FilterType type, float sample_rate,
                          float freq, float q, float gain_db);
