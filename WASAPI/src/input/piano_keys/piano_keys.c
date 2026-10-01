#include "input/piano_keys/piano_keys.h"

#define OCTAVE_MIN  0
#define OCTAVE_MAX  8

void piano_keys_init(AT_PianoKeys* k, int octave) {
    k->octave = octave;
    for (int i = 0; i < AT_SC_COUNT; i++) k->held[i] = -1;
}

// Physical key → semitones above the C of the current octave, -1 = not a note key.
static int key_to_semitone(uint16_t scan) {
    switch (scan) {
    case AT_SC_A:         return 0;     // C
    case AT_SC_W:         return 1;     // C#
    case AT_SC_S:         return 2;     // D
    case AT_SC_E:         return 3;     // D#
    case AT_SC_D:         return 4;     // E
    case AT_SC_F:         return 5;     // F
    case AT_SC_T:         return 6;     // F#
    case AT_SC_G:         return 7;     // G
    case AT_SC_Y:         return 8;     // G#
    case AT_SC_H:         return 9;     // A
    case AT_SC_U:         return 10;    // A#
    case AT_SC_J:         return 11;    // B (German: H)
    case AT_SC_K:         return 12;    // C
    case AT_SC_O:         return 13;    // C#
    case AT_SC_L:         return 14;    // D
    case AT_SC_P:         return 15;    // D#
    case AT_SC_SEMICOLON: return 16;    // E
    default:              return -1;
    }
}

AT_PianoKeysResult piano_keys_event(AT_PianoKeys* k, const AT_KeyEvent* ev, int* note) {
    uint16_t scan = ev->scan_code;
    if (scan >= AT_SC_COUNT) return AT_PIANO_KEYS_NOT_MINE;

    int semitone = key_to_semitone(scan);
    if (semitone >= 0) {
        int* held = &k->held[scan];

        if (ev->down && *held < 0) {                // first press (auto-repeat is ignored)
            *held = 12 * (k->octave + 1) + semitone;
            *note = *held;
            return AT_PIANO_KEYS_NOTE_ON;
        }
        if (!ev->down && *held >= 0) {
            *note = *held;                          // the note THIS key started,
            *held = -1;                             // even if the octave changed since
            return AT_PIANO_KEYS_NOTE_OFF;
        }
        return AT_PIANO_KEYS_USED;
    }

    if (scan == AT_SC_Z || scan == AT_SC_X) {       // octave down / up
        if (ev->down && !ev->repeat) {
            if (scan == AT_SC_Z && k->octave > OCTAVE_MIN) k->octave--;
            if (scan == AT_SC_X && k->octave < OCTAVE_MAX) k->octave++;
        }
        return AT_PIANO_KEYS_USED;
    }

    return AT_PIANO_KEYS_NOT_MINE;
}

bool piano_keys_release_next(AT_PianoKeys* k, int* note) {
    for (int scan = 0; scan < AT_SC_COUNT; scan++) {
        if (k->held[scan] < 0) continue;
        *note         = k->held[scan];
        k->held[scan] = -1;
        return true;
    }
    return false;
}
