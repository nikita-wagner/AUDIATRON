// ═══════════════════════════════════════════════════════════════════════════
//  instruments/fusion/fusion.h — two instruments on one keyboard
//
//  The "ultimate" track: the main keyboard plays one instrument (the layered
//  keyboard) and the numpad plays the drumpad, at the same time. It has no
//  sound of its own: it hands each key to the instrument it belongs to, so the
//  notes and hits sound on THOSE tracks, with their volume, effects and
//  settings, and a recording logs them there.
//
//      numpad 1..9   pads 1..9          numpad 0  pad 10     .  pad 11
//      numpad /      pad 12             *  pad 13            -  pad 14
//      numpad +      pad 15             numpad Enter  pad 16
//
//  Hold a numpad pad and the drumpad's controls act on it (+ / - pitch,
//  arrows, , . volume, L loop, P keep tempo, C V F1 effects, X sync, / cut,
//  Backspace reset). With no pad held, every key except the numpad goes to the
//  keyboard instrument; keys neither uses are left to the app.
//
//  The numpad is told apart from the arrows, Enter and '-' by the console's
//  "extended key" flag, so it works with NumLock on and off.
//
//  Files in this folder:
//    fusion.c        AT_Instrument adapter: status, help, attach, release
//    fusion_keys.c   which key goes where
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "instruments/drumpad/drumpad.h"
#include "instruments/instrument.h"

typedef struct {
    AT_Instrument keyboard;                 // played by the main keyboard (not attached here)
    AT_Instrument drums;                    // played by the numpad
    AT_Drumpad*   drumpad;                  // the same drumpad, to see which pads are held
    char          help[1024];
} AT_Fusion;

// Both instruments must already be on their own tracks; the fusion only plays them.
void fusion_init(AT_Fusion* f, AT_Instrument keyboard, AT_Drumpad* drumpad);

AT_Instrument fusion_instrument(AT_Fusion* f);

// The drumpad pad (0..15) a numpad key plays, -1 = not a numpad pad key.
int  fusion_numpad_pad(const AT_KeyEvent* ev);

// True while a pad is held: the controls then belong to the drums (the app
// uses this to send track keys to the right track).
bool fusion_pad_held(const AT_Fusion* f);

// ─── used between the files of this folder ───────────────────────────────────

bool fusion_key(AT_Fusion* f, const AT_KeyEvent* ev);              // fusion_keys.c
