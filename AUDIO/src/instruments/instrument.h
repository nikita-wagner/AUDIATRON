// ═══════════════════════════════════════════════════════════════════════════
//  instruments/instrument.h — what every playable instrument looks like
//
//  An instrument = a way to PLAY (which key does what) + a SOUND (a source it
//  owns and its settings). Keyboard piano, drumpad, guitar: one folder each,
//  all described to the rest of the program by one AT_Instrument.
//
//  Each instrument sits on its own track (tracks/track_list.h), which attaches
//  it to an engine channel and sends key events to the instrument on the
//  selected track. Nobody needs to know what kind of instrument it is talking to.
//
//  To add one: copy instruments/_template/, then register it in
//  app/app_instruments.c.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stddef.h>

#include "engine/engine.h"
#include "input/key_event.h"

typedef struct {
    const char* name;               // shown in the status line
    const char* help;               // key legend, printed at startup
    void*       self;               // the instrument's own struct

    // Plug the instrument's source into this engine channel and set up its sound.
    void (*attach)(void* self, AT_Engine* engine, uint32_t channel);

    // A key was pressed or released. Return true if the instrument used it,
    // false to let the app handle it (effects, volume, quit...).
    bool (*key)(void* self, const AT_KeyEvent* ev);

    // Let go of every key this instrument is holding (called when another track
    // is selected). Send one note-off per held key, not engine_all_notes_off:
    // a track that is on hold then keeps sounding.
    void (*release_all)(void* self);

    // Short text for the status line, e.g. "oct 4 saw".
    void (*status)(void* self, char* out, size_t cap);
} AT_Instrument;
