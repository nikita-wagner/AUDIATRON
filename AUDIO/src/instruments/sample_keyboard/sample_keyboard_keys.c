// ═══════════════════════════════════════════════════════════════════════════
//  instruments/sample_keyboard/sample_keyboard_keys.c — what each key does
//
//  Runs on the control thread. Notes and parameter changes go to the audio
//  thread through engine_* commands only.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/sample_keyboard/sample_keyboard.h"

#define VELOCITY 0.9f

const char SAMPLE_KEYBOARD_HELP[] =
    PIANO_KEYS_HELP
    "  Y / X  octave -/+      1  gate on/off (note ends with the key)      2  loop on/off\n";

static void send(AT_SampleKeyboard* k, AT_SamplerParam id, float value) {
    engine_set_param(k->engine, sampler_set_param, &k->sampler, (int)id, value);
}

bool sample_keyboard_key(AT_SampleKeyboard* k, const AT_KeyEvent* ev) {
    int note;

    // notes + octave: the shared piano layout
    switch (piano_keys_event(&k->keys, ev, &note)) {
    case AT_PIANO_KEYS_NOTE_ON:  engine_note_on (k->engine, k->channel, note, VELOCITY); return true;
    case AT_PIANO_KEYS_NOTE_OFF: engine_note_off(k->engine, k->channel, note);           return true;
    case AT_PIANO_KEYS_USED:     return true;
    case AT_PIANO_KEYS_NOT_MINE: break;
    }

    if (ev->scan_code == AT_SC_1) {
        if (ev->down && !ev->repeat) { k->gate = !k->gate; send(k, AT_SAMPLER_PARAM_GATE, k->gate ? 1.0f : 0.0f); }
        return true;
    }
    if (ev->scan_code == AT_SC_2) {
        if (ev->down && !ev->repeat) { k->loop = !k->loop; send(k, AT_SAMPLER_PARAM_LOOP, k->loop ? 1.0f : 0.0f); }
        return true;
    }

    return false;       // not ours — the app may use it
}

void sample_keyboard_release_all(AT_SampleKeyboard* k) {
    int note;
    while (piano_keys_release_next(&k->keys, &note))
        engine_note_off(k->engine, k->channel, note);
}
