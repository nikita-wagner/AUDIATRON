// ═══════════════════════════════════════════════════════════════════════════
//  instruments/keyboard_piano/keyboard_piano_keys.c — what each key does
//
//  Runs on the control thread. Notes and parameter changes go to the audio
//  thread through engine_* commands only.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/keyboard_piano/keyboard_piano.h"

#define VELOCITY 0.8f

const char KEYBOARD_PIANO_HELP[] =
    PIANO_KEYS_HELP
    "  Y / X  octave -/+      1..5  waveform (sine saw square triangle noise)\n";

bool keyboard_piano_key(AT_KeyboardPiano* p, const AT_KeyEvent* ev) {
    uint16_t scan = ev->scan_code;
    int      note;

    // notes + octave: the shared piano layout
    switch (piano_keys_event(&p->keys, ev, &note)) {
    case AT_PIANO_KEYS_NOTE_ON:  engine_note_on (p->engine, p->channel, note, VELOCITY); return true;
    case AT_PIANO_KEYS_NOTE_OFF: engine_note_off(p->engine, p->channel, note);           return true;
    case AT_PIANO_KEYS_USED:     return true;
    case AT_PIANO_KEYS_NOT_MINE: break;
    }

    // waveform: keys 1..5 in AT_Waveform order
    if (scan >= AT_SC_1 && scan < AT_SC_1 + AT_WAVE_COUNT) {
        if (ev->down && !ev->repeat) {
            p->wave = (AT_Waveform)(scan - AT_SC_1);
            engine_set_param(p->engine, synth_set_param, &p->synth,
                             AT_SYNTH_PARAM_WAVEFORM, (float)p->wave);
        }
        return true;
    }

    return false;       // not ours — the app may use it
}

void keyboard_piano_release_all(AT_KeyboardPiano* p) {
    int note;
    while (piano_keys_release_next(&p->keys, &note))
        engine_note_off(p->engine, p->channel, note);
}
