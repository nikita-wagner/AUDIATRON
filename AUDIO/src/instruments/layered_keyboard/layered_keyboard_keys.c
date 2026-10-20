// ═══════════════════════════════════════════════════════════════════════════
//  instruments/layered_keyboard/layered_keyboard_keys.c — what each key does
//
//  Runs on the control thread. A note is sent ONCE to the track; the layer
//  stack on the audio thread hands it to every layer.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/layered_keyboard/layered_keyboard.h"

#define VELOCITY 0.8f

bool layered_keyboard_key(AT_LayeredKeyboard* k, const AT_KeyEvent* ev) {
    uint16_t scan = ev->scan_code;
    int      note;

    // notes + octave: the shared piano layout
    switch (piano_keys_event(&k->keys, ev, &note)) {
    case AT_PIANO_KEYS_NOTE_ON:  engine_note_on (k->engine, k->channel, note, VELOCITY); return true;
    case AT_PIANO_KEYS_NOTE_OFF: engine_note_off(k->engine, k->channel, note);           return true;
    case AT_PIANO_KEYS_USED:     return true;
    case AT_PIANO_KEYS_NOT_MINE: break;
    }

    // layer on/off: keys 1..N in layer order
    if (scan >= AT_SC_1 && (uint32_t)(scan - AT_SC_1) < k->layer_count) {
        if (ev->down && !ev->repeat) {
            uint32_t layer = (uint32_t)(scan - AT_SC_1);
            layered_keyboard_set_layer(k, layer, !k->layer_on[layer]);
        }
        return true;
    }

    return false;       // not ours — the app may use it
}

void layered_keyboard_set_layer(AT_LayeredKeyboard* k, uint32_t layer, bool on) {
    if (layer >= k->layer_count) return;
    if (!k->layer_name[layer]) on = false;          // a slot this instrument does not use

    k->layer_on[layer] = on;
    if (k->engine)
        engine_set_param(k->engine, layer_stack_set_param, &k->stack, AT_LAYER_PARAM(layer, AT_LAYER_PARAM_MUTE), on ? 0.0f : 1.0f);
    else
        layer_stack_set_param(&k->stack, AT_LAYER_PARAM(layer, AT_LAYER_PARAM_MUTE), on ? 0.0f : 1.0f);
}

void layered_keyboard_release_all(AT_LayeredKeyboard* k) {
    int note;
    while (piano_keys_release_next(&k->keys, &note))
        engine_note_off(k->engine, k->channel, note);
}
