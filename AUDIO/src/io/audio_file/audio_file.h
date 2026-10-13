// ═══════════════════════════════════════════════════════════════════════════
//  io/audio_file/audio_file.h — load any supported sound file
//
//  One call for every format. The kind is decided by the file CONTENT, not the
//  name (a .mp3 that is really a WAV still works), with the extension only
//  used to produce a good error message.
//
//  Supported: WAV (io/wav_reader), MP3 (io/mp3_reader).
//  To add a format (OGG, FLAC, ...): write a reader with the AT_ClipLoader
//  shape in its own io/ folder and add one line to audio_file.c.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "io/audio_clip/audio_clip.h"

// false = failed; `error` says why ("cannot open …", "WAV: …", "not a decodable MP3 file").
bool audio_file_load(const char* path, AT_Clip* out, char* error, size_t error_cap);

// Same, from memory (e.g. a file you already read).
bool audio_file_load_memory(const uint8_t* bytes, size_t size, AT_Clip* out, char* error, size_t error_cap);
