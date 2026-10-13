// ═══════════════════════════════════════════════════════════════════════════
//  io/mp3_reader/mp3_reader.h — MP3 file → AT_Clip
//
//  Decodes MPEG-1/2/2.5 Layer III (mono or stereo, constant or variable bit
//  rate, with or without ID3 tags) completely into memory using the vendored
//  minimp3 decoder (third_party/minimp3, public domain).
//
//  The decoder is built without SIMD, so it is plain portable C and runs on
//  any CPU the rest of the engine runs on. Decoding takes a fraction of the
//  song's length on a modern PC; allow several times more on a Pentium.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "io/audio_clip/audio_clip.h"

// AT_ClipLoader
bool mp3_load(const char* path, AT_Clip* out, char* error, size_t error_cap);

// The same, from memory.
bool mp3_load_memory(const uint8_t* bytes, size_t size, AT_Clip* out, char* error, size_t error_cap);
