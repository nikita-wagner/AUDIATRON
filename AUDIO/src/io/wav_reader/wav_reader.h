// ═══════════════════════════════════════════════════════════════════════════
//  io/wav_reader/wav_reader.h — WAV file → AT_Clip
//
//  Reads uncompressed PCM (8 / 16 / 24 / 32 bit), IEEE float (32 / 64 bit) and
//  WAVE_FORMAT_EXTENSIBLE wrappers of those, with any number of channels
//  (only the first two are kept: front left and right). Skips unknown chunks
//  (LIST, cue, ...), handles odd chunk padding, and tolerates a wrong data
//  size (files cut short, or written by a recorder that never went back to
//  fill in the header).
//
//  The result is 16-bit; 24-bit / float sources are rounded to it.
//  Compressed WAVs (ADPCM, MP3-in-WAV, ...) are refused with a clear message.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "io/audio_clip/audio_clip.h"

// AT_ClipLoader
bool wav_load(const char* path, AT_Clip* out, char* error, size_t error_cap);

// The same, from memory (file contents). Lets tests and embedded sounds skip the disk.
bool wav_load_memory(const uint8_t* bytes, size_t size, AT_Clip* out, char* error, size_t error_cap);
