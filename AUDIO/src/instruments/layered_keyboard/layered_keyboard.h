// ═══════════════════════════════════════════════════════════════════════════
//  instruments/layered_keyboard/layered_keyboard.h — layers into ONE sound,
//  and which instrument they are (piano, guitar, strings, violin, contrabass,
//  trumpet, ...: layered_keyboard_patch.c)
//
//  Every note you play sounds on all layers at once (layers/layer_stack.h).
//  Played with the shared piano layout (input/piano_keys/piano_keys.h), plus:
//
//      1..N   layer N on/off, in the order of the table in layered_keyboard_patch.c
//
//  Files in this folder:
//    layered_keyboard.c         init, attach to the engine, AT_Instrument
//    layered_keyboard_keys.c    what each key does
//    layered_keyboard_patch.c   ◄ THE INSTRUMENTS: a row per layer, + channel EQ
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "effects/tremolo/tremolo.h"
#include "input/piano_keys/piano_keys.h"
#include "instruments/instrument.h"
#include "layers/layer_stack.h"
#include "synth/synth.h"

#define AT_LAYERED_KEYS_MAX 9           // layer slots of a layered keyboard (its stack could hold more)
#define AT_LAYERED_KEYS_KEYED 8         // the first this many switch on / off with keys 1..8 (9 is the app's)

typedef struct {
    AT_LayerStack stack;                        // the sound source (rendered by the engine)
    AT_Synth      synths[AT_LAYERED_KEYS_MAX];  // one sound generator per layer
    AT_Tremolo    motion[AT_LAYERED_KEYS_MAX];  // per-layer insert effect (used where the table asks for it)

    // control-thread side
    AT_Engine*    engine;                       // set by attach
    uint32_t      channel;
    AT_PianoKeys  keys;                         // octave + which key holds which note
    uint32_t      layer_count;                  // slots (the instrument may leave some silent)
    int           instrument;                   // layered_keyboard_set_instrument; -1 = built by hand ("custom")
    char          custom_name[40];              // a built one's name ("" = "Custom")
    const char*   layer_name[AT_LAYERED_KEYS_MAX];  // NULL = a slot this instrument does not use (else its label)
    char          label[AT_LAYERED_KEYS_MAX][24];
    bool          layer_on[AT_LAYERED_KEYS_MAX];
    char          help[320];
} AT_LayeredKeyboard;

void layered_keyboard_init(AT_LayeredKeyboard* k, float sample_rate);

// Describes the instrument to the app. `k` must outlive the returned struct's use.
AT_Instrument layered_keyboard_instrument(AT_LayeredKeyboard* k);

// Switches one layer on or off while playing (control thread). A slot the instrument does not use stays off.
void layered_keyboard_set_layer(AT_LayeredKeyboard* k, uint32_t layer, bool on);

// Which instrument the layers are (control thread, also while playing): every slot's synth, tremolo, volume,
// pan, transpose and on / off as the instrument has them. Everything stays adjustable afterwards.
int         layered_keyboard_instrument_count(void);
const char* layered_keyboard_instrument_name(int i);          // "Acoustic Guitar"
const char* layered_keyboard_instrument_word(int i);          // "guitar" (project files)
int         layered_keyboard_instrument_find(const char* word);   // -1 = none
void        layered_keyboard_set_instrument(AT_LayeredKeyboard* k, int i);   // i = -1: nothing changes

// Building your own (control thread, also while playing). Every instrument's layers are voices to pick from: a
// slot takes one (its synth, tremolo, volume, pan, transpose) and the keyboard becomes "custom".
int         layered_keyboard_voice_count(int instrument);                 // the layers it uses
const char* layered_keyboard_voice_name(int instrument, int voice);
void        layered_keyboard_set_voice(AT_LayeredKeyboard* k, uint32_t slot, int instrument, int voice);
// A slot in use with this label, or (label NULL / "") not used: silent. Its sound is not touched.
void        layered_keyboard_set_slot(AT_LayeredKeyboard* k, uint32_t slot, const char* label);

// ─── used between the files of this folder ───────────────────────────────────

bool layered_keyboard_key        (AT_LayeredKeyboard* k, const AT_KeyEvent* ev);    // layered_keyboard_keys.c
void layered_keyboard_release_all(AT_LayeredKeyboard* k);                           // layered_keyboard_keys.c
void layered_keyboard_build_layers (AT_LayeredKeyboard* k, float sample_rate);      // layered_keyboard_patch.c
void layered_keyboard_setup_channel(AT_LayeredKeyboard* k);                         // layered_keyboard_patch.c
