// ═══════════════════════════════════════════════════════════════════════════
//  input/piano_keys/piano_keys.h — the computer keyboard as a piano keyboard
//
//  Two key rows laid out like a piano, 17 semitones from C:
//
//      black:   W E   T Z U   O P          (QWERTZ labels)
//      white:  A S D F G H J K L Ö
//               C D E F G A H C D E
//
//      Y / X   octave down / up
//
//  That is the default LAYOUT. Any key can play any note: piano_keys_set_layout
//  takes a key per note (up to two octaves) and the two octave keys, for every
//  instrument played like a piano at once. A layout can also give single notes
//  a key of their own (fixed): that key plays exactly that note (C2, C5, ...),
//  whatever octave the other keys are in.
//
//  Pure input logic, shared by every instrument that is played like a piano.
//  It turns key events into "start note N" / "stop note N" and remembers which
//  key started which note. It does not know the engine: the instrument that
//  owns it decides what a note does.
//
//      int note;
//      switch (piano_keys_event(&keys, ev, &note)) {
//      case AT_PIANO_KEYS_NOTE_ON:  engine_note_on (engine, channel, note, velocity); return true;
//      case AT_PIANO_KEYS_NOTE_OFF: engine_note_off(engine, channel, note);           return true;
//      case AT_PIANO_KEYS_USED:     return true;
//      case AT_PIANO_KEYS_NOT_MINE: break;      // free for the instrument's own controls
//      }
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "input/key_event.h"

// First help line of every instrument that uses this layout.
#define PIANO_KEYS_HELP "  A W S E D F T G Z H U J K O L P Oe   play notes (QWERTZ labels)\n"

#define AT_PIANO_KEYS_NOTES 24          // note keys a layout can have: two octaves from C
#define AT_PIANO_KEYS_FIXED 132         // fixed keys: one per note C-1 .. B9 (MIDI's 0..127 and the rest of octave 9)

// Which key plays which note: scan codes (key_event.h), 0 = no key. Keys of
// the main keyboard and the numpad; "extended" keys (arrows, ...) are not used.
typedef struct {
    uint16_t note[AT_PIANO_KEYS_NOTES];     // the keys for C, C#, D, ... of the octave and the next
    uint16_t octave_down;
    uint16_t octave_up;
    uint16_t fixed[AT_PIANO_KEYS_FIXED];    // a key for exactly this note (fixed[60] = C4), 0 = none
} AT_PianoLayout;

void                  piano_keys_default_layout(AT_PianoLayout* out);   // the one drawn above
const AT_PianoLayout* piano_keys_layout(void);
void                  piano_keys_set_layout(const AT_PianoLayout* layout);   // control thread; NULL = the default

typedef enum {
    AT_PIANO_KEYS_NOT_MINE,     // not a piano key
    AT_PIANO_KEYS_USED,         // a piano key with nothing to do (auto-repeat, octave change)
    AT_PIANO_KEYS_NOTE_ON,      // *note has to start
    AT_PIANO_KEYS_NOTE_OFF      // *note has to stop
} AT_PianoKeysResult;

typedef struct {
    int octave;                 // octave of the leftmost key (4 = C4)
    int held[AT_SC_COUNT];      // scan code → MIDI note it started, -1 = none
} AT_PianoKeys;

void piano_keys_init(AT_PianoKeys* k, int octave);

AT_PianoKeysResult piano_keys_event(AT_PianoKeys* k, const AT_KeyEvent* ev, int* note);

// Forgets one held key per call and reports the note it had started; false
// when none is left. For releasing everything, e.g. when the instrument loses
// the keyboard:
//     int note;
//     while (piano_keys_release_next(&keys, &note)) engine_note_off(engine, channel, note);
bool piano_keys_release_next(AT_PianoKeys* k, int* note);
