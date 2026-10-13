// ═══════════════════════════════════════════════════════════════════════════
//  io/audio_clip/audio_clip.h — a sound file decoded into memory
//
//  The common result of every file reader (WAV, MP3, ...) and the input of the
//  sampler. Samples are 16-bit signed, interleaved, mono or stereo, at the
//  file's own sample rate — the sampler converts the rate while playing.
//
//  Memory: frames × channels × 2 bytes. A 3 minute stereo song at 44.1 kHz is
//  about 32 MB, so on small machines (a Windows 98 PC may have 64 MB) use
//  short files or mono. Streaming from disk is on the roadmap.
//
//  Ownership: a clip owns its data; free it with audio_clip_free().
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int16_t* data;              // frames * channels samples, interleaved
    uint32_t frames;
    uint32_t channels;          // 1 or 2
    uint32_t sample_rate;       // Hz
} AT_Clip;

// Every reader has this shape. false = failed, `error` says why (may be NULL).
typedef bool (*AT_ClipLoader)(const char* path, AT_Clip* out, char* error, size_t error_cap);

// Allocates a clip of the given size (samples zeroed). false = out of memory.
bool audio_clip_alloc(AT_Clip* c, uint32_t frames, uint32_t channels, uint32_t sample_rate);

// Takes over memory that was allocated with malloc (e.g. by a decoder).
void audio_clip_adopt(AT_Clip* c, int16_t* data, uint32_t frames, uint32_t channels, uint32_t sample_rate);

void audio_clip_free(AT_Clip* c);                       // safe on an empty clip

static inline double audio_clip_seconds(const AT_Clip* c) {
    return c->sample_rate ? (double)c->frames / (double)c->sample_rate : 0.0;
}

// Writes "message" into error if it is not NULL. Used by the readers.
void audio_clip_set_error(char* error, size_t cap, const char* fmt, ...);

// Reads a whole file into a malloc'ed buffer. NULL = failed (error filled in).
uint8_t* audio_clip_read_file(const char* path, size_t* size, char* error, size_t error_cap);
