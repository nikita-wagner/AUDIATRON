// ═══════════════════════════════════════════════════════════════════════════
//  core/at_math.h — small math helpers shared by all DSP modules
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <math.h>
#include <xmmintrin.h>

#define AT_PI      3.14159265358979323846
#define AT_TWO_PI  (2.0 * AT_PI)

static inline float at_clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

static inline float at_db_to_gain(float db)   { return powf(10.0f, db / 20.0f); }
static inline float at_gain_to_db(float gain) { return gain > 1e-9f ? 20.0f * log10f(gain) : -180.0f; }

static inline float at_note_to_freq(int midi_note) {
    return 440.0f * powf(2.0f, (float)(midi_note - 69) / 12.0f);
}

// Frequency ratio of a pitch offset in cents (100 cents = one semitone).
static inline float at_cents_to_ratio(float cents) {
    return powf(2.0f, cents / 1200.0f);
}

// Coefficient k for a one-pole smoother:  value += (target - value) * k
// Reaches ~63 % of a step after `seconds`.
static inline float at_smoothing_coef(float seconds, float sample_rate) {
    return 1.0f - expf(-1.0f / (seconds * sample_rate));
}

// Denormals (tiny floats left behind by decaying filters/envelopes) are up to
// 100x slower on x86. Every thread that runs DSP calls this once.
static inline void at_enable_flush_to_zero(void) {
    _mm_setcsr(_mm_getcsr() | 0x8040);     // FTZ (bit 15) | DAZ (bit 6)
}
