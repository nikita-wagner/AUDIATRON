// ═══════════════════════════════════════════════════════════════════════════
//  dsp/oscillator/oscillator.h — one band-limited oscillator
//
//  A building block, not an instrument: it knows nothing about notes, voices
//  or envelopes. Use it for synth voices, LFOs, test tones, ...
//
//  Phase is normalized (0..1) and kept in double so long notes don't drift.
//  Saw and square use PolyBLEP, which smooths the one-sample discontinuity
//  and removes most of the aliasing a naive saw produces at high notes.
//  Sine reads a table, as sinf for every sample was the dearest of the waves.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdint.h>

#include "core/at_math.h"

#define AT_SINE_SIZE 4096       // the table's steps round one cycle: a power of 2

// One cycle of a sine, then its first value again: a read between the last two
// needs no wrap. The first oscillator_init fills it (never the audio thread).
extern float at_sine_table[AT_SINE_SIZE + 1];

typedef enum {
    AT_WAVE_SINE,
    AT_WAVE_SAW,        // PolyBLEP band-limited
    AT_WAVE_SQUARE,     // PolyBLEP band-limited
    AT_WAVE_TRIANGLE,
    AT_WAVE_CIRCLE,
    AT_WAVE_PLUCK,      // a plucked string: the synth plays it (synth.c), the oscillator gives silence
    AT_WAVE_COUNT
} AT_Waveform;

typedef struct {
    double   phase;         // normalized 0..1
    double   phase_inc;     // frequency / sample_rate
    uint32_t noise_state;   // xorshift32 state, only used by AT_WAVE_NOISE
} AT_Oscillator;

const char* oscillator_waveform_name(AT_Waveform w);

// noise_seed: give every oscillator a different one, or their noise correlates.
void oscillator_init(AT_Oscillator* o, uint32_t noise_seed);

static inline void oscillator_set_frequency(AT_Oscillator* o, double hz, double sample_rate) {
    o->phase_inc = hz / sample_rate;
}

static inline float oscillator_poly_blep(double t, double dt) {
    if (t < dt)       { t /= dt;               return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return (float)(t * t + t + t + 1.0); }
    return 0.0f;
}

// sin(2 pi t), read between the table's two nearest values. The mask keeps even
// a broken phase (NaN, below 0) on the table, as this runs on the audio thread.
static inline float oscillator_sine(double t) {
    const double p     = t * AT_SINE_SIZE;
    const int    whole = (int)p;
    const float  part  = (float)(p - whole);
    const float* at    = at_sine_table + (whole & (AT_SINE_SIZE - 1));
    return at[0] + (at[1] - at[0]) * part;
}

// Returns the sample at the current phase (-1..+1), then advances one frame.
static inline float oscillator_tick(AT_Oscillator* o, AT_Waveform wave) {
    double t  = o->phase;
    double dt = o->phase_inc;
    float  y;

    switch (wave) {
    case AT_WAVE_SINE:
        y = oscillator_sine(t);
        break;
    case AT_WAVE_SAW:
        y = (float)(2.0 * t - 1.0) - oscillator_poly_blep(t, dt);
        break;
    case AT_WAVE_SQUARE: {
        double t2 = t + 0.5;
        if (t2 >= 1.0) t2 -= 1.0;
        y = (t < 0.5 ? 1.0f : -1.0f) + oscillator_poly_blep(t, dt) - oscillator_poly_blep(t2, dt);
        break;
    }
    case AT_WAVE_TRIANGLE:
        y = (float)(1.0 - 4.0 * fabs(t - 0.5));
        break;
    case AT_WAVE_CIRCLE:
        y = (float)(sqrt(1.0 - (t - 0.5) * (t - 0.5) * 4.0) * 2.0 - 1.0);
        break;
    default:
        y = 0.0f;
        break;
    }

    o->phase += o->phase_inc;
    if (o->phase >= 1.0) o->phase -= floor(o->phase);    // a note above the sample rate steps more than one cycle
    return y;
}
