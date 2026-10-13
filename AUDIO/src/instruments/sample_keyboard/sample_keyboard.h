// ═══════════════════════════════════════════════════════════════════════════
//  instruments/sample_keyboard/sample_keyboard.h — a sound file played on the keys
//
//  The file is pitched across the keyboard, like a hardware sampler: every key
//  plays it at its own pitch, several keys at once. Natural for short sounds
//  (a kick becomes a bass line, a vocal "ah" becomes a choir).
//
//  Played with the shared piano layout (input/piano_keys/piano_keys.h):
//  the key for C of the current octave plays the file at its ORIGINAL pitch
//  when the octave is 4 (the start-up value); every semitone up or down changes
//  pitch and speed together, like a record played faster or slower.
//
//      1   one-shot / gated:  a one-shot plays to the end of the file whatever
//                             you do; gated stops when you let go of the key
//      2   loop on / off:     a looped note repeats until released (use with gate)
//      Y / X   octave -/+
//
//  Files in this folder:
//    sample_keyboard.c        load the file, attach to the engine, AT_Instrument
//    sample_keyboard_keys.c   what each key does
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "input/piano_keys/piano_keys.h"
#include "instruments/instrument.h"
#include "io/audio_clip/audio_clip.h"
#include "sampler/sampler.h"

typedef struct {
    AT_Clip      clip;                      // the decoded file (owned unless this is a copy)
    bool         owns_clip;
    AT_Sampler   sampler;                   // the sound source (rendered by the engine)

    AT_Engine*   engine;                    // NULL until attach
    uint32_t     channel;
    AT_PianoKeys keys;                      // octave + which key holds which note
    bool         gate;
    bool         loop;
    char         label[40];
} AT_SampleKeyboard;

// Loads the file. false = failed, `error` says why. Free with sample_keyboard_free().
bool sample_keyboard_init(AT_SampleKeyboard* k, const char* path, float sample_rate, char* error, size_t error_cap);
void sample_keyboard_free(AT_SampleKeyboard* k);

// A second sampler keyboard on the same file: shares the decoded audio, fresh
// settings. `original` must outlive the copy.
void sample_keyboard_init_copy(AT_SampleKeyboard* k, const AT_SampleKeyboard* original, float sample_rate);

// Every note plays only from..to of the file (fractions of its length, e.g.
// 0.2 .. 0.9). false = out of order / outside 0..1.
bool sample_keyboard_set_cut(AT_SampleKeyboard* k, float from, float to);

AT_Instrument sample_keyboard_instrument(AT_SampleKeyboard* k);

// ─── used between the files of this folder ───────────────────────────────────

bool sample_keyboard_key        (AT_SampleKeyboard* k, const AT_KeyEvent* ev);      // sample_keyboard_keys.c
void sample_keyboard_release_all(AT_SampleKeyboard* k);                             // sample_keyboard_keys.c
extern const char SAMPLE_KEYBOARD_HELP[];                                           // sample_keyboard_keys.c
