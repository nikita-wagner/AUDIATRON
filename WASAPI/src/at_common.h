// ═══════════════════════════════════════════════════════════════════════════
//  at_common.h — shared constants and the planar audio buffer type
//
//  Everything inside the engine is 32-bit float, PLANAR (one array per
//  channel), nominal range -1.0 .. +1.0. Conversion to whatever the device
//  wants (float32 / int16 / int24 / int32, interleaved, 2..8 channels)
//  happens exactly once, in wasapi_backend.c, after the last hook has run.
//
//  Compared to the WinMM engine (interleaved int16 + clamping in every
//  mix_stereo call) this means: no clipping between stages, full headroom
//  for EQ boosts, and every stage can touch a single channel without
//  de-interleaving first.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <xmmintrin.h>

#define AT_MAX_CHANNELS         2       // engine bus width (stereo)
#define AT_MAX_BLOCK_FRAMES     1024    // largest block the engine renders in one go
#define AT_MAX_VOICES           32      // polyphony per synth
#define AT_MAX_MIX_CHANNELS     8       // mixer strips (each owns one synth)
#define AT_MAX_EQ_BANDS         8       // biquad bands per equalizer
#define AT_MAX_HOOKS_PER_POINT  4       // chained buffer hooks per hook point
#define AT_QUEUE_CAPACITY       256     // control → audio commands (power of two)

#define AT_PI      3.14159265358979323846
#define AT_TWO_PI  (2.0 * AT_PI)

// ─── Planar buffer ───────────────────────────────────────────────────────────

typedef struct {
    float*   ch[AT_MAX_CHANNELS];   // ch[c][i] = sample i of channel c
    uint32_t channels;
    uint32_t frames;
    uint32_t sample_rate;
    uint64_t frame_clock;           // absolute frame index of sample 0 (sample-accurate time)
} AT_Buffer;

// The one callback the platform layer knows about. The backend hands the
// engine an empty stereo AT_Buffer (frames <= AT_MAX_BLOCK_FRAMES) and
// expects it filled. Runs on the audio thread.
typedef void (*AT_RenderCallback)(AT_Buffer* out, void* user);

static inline void at_buffer_clear(AT_Buffer* b) {
    for (uint32_t c = 0; c < b->channels; c++)
        memset(b->ch[c], 0, b->frames * sizeof(float));
}

// dst += src * gain. Both buffers must have the same frame count.
static inline void at_buffer_mix(AT_Buffer* dst, const AT_Buffer* src, float gain) {
    for (uint32_t c = 0; c < dst->channels && c < src->channels; c++)
        for (uint32_t i = 0; i < dst->frames; i++)
            dst->ch[c][i] += src->ch[c][i] * gain;
}

// View of `frames` frames starting at `offset` — no copy, same memory.
static inline AT_Buffer at_buffer_slice(const AT_Buffer* b, uint32_t offset, uint32_t frames) {
    AT_Buffer s = *b;
    for (uint32_t c = 0; c < b->channels; c++) s.ch[c] = b->ch[c] + offset;
    s.frames      = frames;
    s.frame_clock = b->frame_clock + offset;
    return s;
}

// ─── Small math helpers ──────────────────────────────────────────────────────

static inline float at_clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

static inline float at_db_to_gain(float db)   { return powf(10.0f, db / 20.0f); }
static inline float at_gain_to_db(float gain) { return gain > 1e-9f ? 20.0f * log10f(gain) : -180.0f; }

static inline float at_note_to_freq(int midi_note) {
    return 440.0f * powf(2.0f, (float)(midi_note - 69) / 12.0f);
}

// Denormals (tiny floats left behind by decaying filters/envelopes) are up to
// 100x slower on x86. Every thread that runs DSP calls this once.
static inline void at_enable_flush_to_zero(void) {
    _mm_setcsr(_mm_getcsr() | 0x8040);     // FTZ (bit 15) | DAZ (bit 6)
}
