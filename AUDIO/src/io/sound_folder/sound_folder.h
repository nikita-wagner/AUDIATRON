// ═══════════════════════════════════════════════════════════════════════════
//  io/sound_folder/sound_folder.h — the WAV and MP3 files in a folder
//
//  Lists the files whose name ends in .wav or .mp3 (any case), sorted by name
//  without regard to case, so "01 kick.wav" comes before "02 snare.mp3". It
//  does not open the files; io/audio_file decides later whether they load.
//
//      char paths[16][AT_SOUND_FOLDER_PATH];
//      uint32_t n = sound_folder_list("sample", paths, 16);
//
//  Uses FindFirstFileA, which exists on every Windows since 95.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AT_SOUND_FOLDER_PATH  260           // MAX_PATH

// true = `folder` exists and is a folder.
bool sound_folder_exists(const char* folder);

// Writes up to `max` paths ("folder\name") in name order; returns how many.
// Only the first 256 files (in the order Windows finds them) are considered.
uint32_t sound_folder_list(const char* folder, char (*paths)[AT_SOUND_FOLDER_PATH], uint32_t max);

// The same for any extension, e.g. ".txt" (any case).
uint32_t sound_folder_list_ext(const char* folder, const char* extension, char (*paths)[AT_SOUND_FOLDER_PATH], uint32_t max);
