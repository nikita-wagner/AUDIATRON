#include "input/piano_keys/piano_keys.h"

#define OCTAVE_MIN  0
#define OCTAVE_MAX  8

void piano_keys_init(AT_PianoKeys* k, int octave) {
    k->octave = octave;
    for (int i = 0; i < AT_SC_COUNT; i++) k->held[i] = -1;
}

// ─── The layout ──────────────────────────────────────────────────────────────

static AT_PianoLayout s_layout;
static bool           s_layout_set;

void piano_keys_default_layout(AT_PianoLayout* out) {
    static const uint16_t notes[17] = {
        AT_SC_A,    // C
        AT_SC_W,    // C#
        AT_SC_S,    // D
        AT_SC_E,    // D#
        AT_SC_D,    // E
        AT_SC_F,    // F
        AT_SC_T,    // F#
        AT_SC_G,    // G
        AT_SC_Y,    // G#  (German: Z)
        AT_SC_H,    // A
        AT_SC_U,    // A#
        AT_SC_J,    // B   (German: H)
        AT_SC_K,    // C
        AT_SC_O,    // C#
        AT_SC_L,    // D
        AT_SC_P,    // D#
        AT_SC_SEMICOLON   // E (German: Oe)
    };
    for (int i = 0; i < AT_PIANO_KEYS_NOTES; i++) out->note[i] = i < 17 ? notes[i] : 0;
    for (int i = 0; i < AT_PIANO_KEYS_FIXED; i++) out->fixed[i] = 0;
    out->octave_down = AT_SC_Z;     // German: Y
    out->octave_up   = AT_SC_X;
}

const AT_PianoLayout* piano_keys_layout(void) {
    if (!s_layout_set) { piano_keys_default_layout(&s_layout); s_layout_set = true; }
    return &s_layout;
}

void piano_keys_set_layout(const AT_PianoLayout* layout) {
    if (layout) s_layout = *layout;
    else        piano_keys_default_layout(&s_layout);
    s_layout_set = true;
}

// Physical key → semitones above the C of the current octave, -1 = not a note key.
static int key_to_semitone(uint16_t scan) {
    const AT_PianoLayout* l = piano_keys_layout();
    for (int i = 0; scan && i < AT_PIANO_KEYS_NOTES; i++)
        if (l->note[i] == scan) return i;
    return -1;
}

// Physical key → the note it always plays, -1 = no fixed key.
static int key_to_fixed(uint16_t scan) {
    const AT_PianoLayout* l = piano_keys_layout();
    for (int i = 0; scan && i < AT_PIANO_KEYS_FIXED; i++)
        if (l->fixed[i] == scan) return i;
    return -1;
}

AT_PianoKeysResult piano_keys_event(AT_PianoKeys* k, const AT_KeyEvent* ev, int* note) {
    uint16_t scan = ev->scan_code;
    if (scan >= AT_SC_COUNT || ev->extended) return AT_PIANO_KEYS_NOT_MINE;

    int semitone = key_to_semitone(scan);
    int fixed    = semitone < 0 ? key_to_fixed(scan) : -1;
    if (semitone >= 0 || fixed >= 0) {
        int* held = &k->held[scan];

        if (ev->down && *held < 0) {                // first press (auto-repeat is ignored)
            *held = fixed >= 0 ? fixed : 12 * (k->octave + 1) + semitone;
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

    const AT_PianoLayout* l = piano_keys_layout();
    if (scan && (scan == l->octave_down || scan == l->octave_up)) {     // octave down / up
        if (ev->down && !ev->repeat) {
            if (scan == l->octave_down && k->octave > OCTAVE_MIN) k->octave--;
            if (scan == l->octave_up && k->octave < OCTAVE_MAX) k->octave++;
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
