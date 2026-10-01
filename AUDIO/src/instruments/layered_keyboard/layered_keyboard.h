// ═══════════════════════════════════════════════════════════════════════════
//  instruments/layered_keyboard/layered_keyboard.h — piano + strings + synth
//  layered into ONE sound
//
//  Every note you play sounds on all layers at once (layers/layer_stack.h).
//  Played with the shared piano layout (input/piano_keys/piano_keys.h), plus:
//
//      1..N   layer N on/off, in the order of the table in layered_keyboard_patch.c
//
//  Files in this folder:
//    layered_keyboard.c         init, attach to the engine, AT_Instrument
//    layered_keyboard_keys.c    what each key does
//    layered_keyboard_patch.c   ◄ THE LAYERS: one table row per layer, + channel EQ
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "effects/tremolo/tremolo.h"
#include "input/piano_keys/piano_keys.h"
#include "instruments/instrument.h"
#include "layers/layer_stack.h"
#include "synth/synth.h"

typedef struct {
    AT_LayerStack stack;                        // the sound source (rendered by the engine)
    AT_Synth      synths[AT_LAYER_MAX];         // one sound generator per layer
    AT_Tremolo    motion[AT_LAYER_MAX];         // per-layer insert effect (used where the table asks for it)

    // control-thread side
    AT_Engine*    engine;                       // set by attach
    uint32_t      channel;
    AT_PianoKeys  keys;                         // octave + which key holds which note
    uint32_t      layer_count;
    const char*   layer_name[AT_LAYER_MAX];
    bool          layer_on[AT_LAYER_MAX];
    char          help[256];
} AT_LayeredKeyboard;

void layered_keyboard_init(AT_LayeredKeyboard* k, float sample_rate);

// Describes the instrument to the app. `k` must outlive the returned struct's use.
AT_Instrument layered_keyboard_instrument(AT_LayeredKeyboard* k);

// Switches one layer on or off while playing (control thread).
void layered_keyboard_set_layer(AT_LayeredKeyboard* k, uint32_t layer, bool on);

// ─── used between the files of this folder ───────────────────────────────────

bool layered_keyboard_key        (AT_LayeredKeyboard* k, const AT_KeyEvent* ev);    // layered_keyboard_keys.c
void layered_keyboard_release_all(AT_LayeredKeyboard* k);                           // layered_keyboard_keys.c
void layered_keyboard_build_layers (AT_LayeredKeyboard* k, float sample_rate);      // layered_keyboard_patch.c
void layered_keyboard_setup_channel(AT_LayeredKeyboard* k);                         // layered_keyboard_patch.c
