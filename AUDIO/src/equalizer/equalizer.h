// ═══════════════════════════════════════════════════════════════════════════
//  equalizer/equalizer.h — parametric EQ: a serial chain of biquad bands
//
//  Up to AT_EQ_BANDS bands (dsp/biquad), each with its own type, frequency,
//  Q and gain. Bands of type AT_FILTER_OFF cost nothing. Works on any
//  AT_Buffer (mono or stereo) in place; knows nothing about the engine.
//
//  Threading: audio-thread only once it is part of a running engine — from the
//  control thread use engine_set_eq_band / engine_set_eq_bypass.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"
#include "dsp/biquad/biquad.h"

#define AT_EQ_BANDS 8

typedef struct {
    AT_FilterType   type;
    float           freq;           // Hz
    float           q;
    float           gain_db;        // PEAK / SHELF only
    AT_BiquadCoeffs coeffs;         // in use
    AT_BiquadCoeffs target;         // a retuned band glides here over ~10 ms (no zipper noise on sweeps)
    uint32_t        glide;          // frames left of the glide, 0 = at the target
    AT_BiquadState  state[AT_MAX_CHANNELS];
} AT_EQBand;

typedef struct {
    AT_EQBand bands[AT_EQ_BANDS];
    bool      bypass;
    float     sample_rate;
} AT_Equalizer;

void eq_init    (AT_Equalizer* eq, float sample_rate);
void eq_reset   (AT_Equalizer* eq);                     // clear filter memory
void eq_set_band(AT_Equalizer* eq, uint32_t band, AT_FilterType type,
                 float freq, float q, float gain_db);

// In place, all channels of buf.
void eq_process(AT_Equalizer* eq, AT_Buffer* buf);
