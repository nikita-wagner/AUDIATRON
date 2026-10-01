// ═══════════════════════════════════════════════════════════════════════════
//  core/at_buffer.h — the planar float buffer every module speaks
//
//  Everything inside the engine is 32-bit float, PLANAR (one array per
//  channel), nominal range -1.0 .. +1.0. Conversion to whatever the device
//  wants happens exactly once, in io/sample_format, after the last hook ran.
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

#include "core/at_config.h"

typedef struct {
    float*   ch[AT_MAX_CHANNELS];   // ch[c][i] = sample i of channel c
    uint32_t channels;
    uint32_t frames;
    uint32_t sample_rate;
    uint64_t frame_clock;           // absolute frame index of sample 0 (sample-accurate time)
} AT_Buffer;

// THE signature of this code base. Effects, sources and the engine's render
// entry all look like this, so any of them can be plugged anywhere a buffer
// flows. `user` is the module's own state struct.
typedef void (*AT_ProcessFn)(AT_Buffer* buf, void* user);

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
