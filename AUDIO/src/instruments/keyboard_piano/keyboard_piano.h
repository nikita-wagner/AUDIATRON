// ═══════════════════════════════════════════════════════════════════════════
//  instruments/keyboard_piano/keyboard_piano.h — piano on the computer keyboard
//
//  Played with the shared piano layout (input/piano_keys/piano_keys.h), plus:
//
//      1..5   waveform
//
//  Files in this folder:
//    keyboard_piano.c         init, attach to the engine, AT_Instrument
//    keyboard_piano_keys.c    what each key does
//    keyboard_piano_patch.c   the sound: synth settings + channel EQ
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "input/piano_keys/piano_keys.h"
#include "instruments/instrument.h"
#include "synth/synth.h"

typedef struct {
    AT_Synth     synth;                 // the sound source (rendered by the engine)
    AT_Engine*   engine;                // set by attach
    uint32_t     channel;
    AT_PianoKeys keys;                  // octave + which key holds which note
    AT_Waveform  wave;
} AT_KeyboardPiano;

void keyboard_piano_init(AT_KeyboardPiano* p, float sample_rate);

// Describes the piano to the app. `p` must outlive the returned struct's use.
AT_Instrument keyboard_piano_instrument(AT_KeyboardPiano* p);

// ─── used between the files of this folder ───────────────────────────────────

bool keyboard_piano_key        (AT_KeyboardPiano* p, const AT_KeyEvent* ev);    // keyboard_piano_keys.c
void keyboard_piano_release_all(AT_KeyboardPiano* p);                           // keyboard_piano_keys.c
void keyboard_piano_load_patch (AT_KeyboardPiano* p);                           // keyboard_piano_patch.c

extern const char KEYBOARD_PIANO_HELP[];                                        // keyboard_piano_keys.c
