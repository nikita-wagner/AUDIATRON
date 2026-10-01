// ═══════════════════════════════════════════════════════════════════════════
//  core/at_module.h — the contracts a pluggable module implements
//
//  The engine knows no concrete synth, sampler or effect. It only knows these
//  shapes, so a new module never requires an engine change:
//
//    effect   = one AT_ProcessFn that changes a buffer in place
//               → plug in with engine_set_hook()
//    source   = one AT_SourceType (render + optional note handlers)
//               → plug in with engine_set_source()
//    any live parameter change = one AT_ParamFn
//               → send with engine_set_param(), which runs it on the audio
//                 thread between two blocks (never while the module processes)
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

// self = the module's state struct, param = an enum the module defines.
typedef void (*AT_ParamFn)(void* self, int param, float value);

// Describes one KIND of sound generator. Declare a single static const
// instance per kind (see synth/synth_source.c); the instance data travels
// separately as `self`.
typedef struct {
    const char*  name;
    AT_ProcessFn render;                                            // required: fill the cleared buffer
    void (*note_on)      (void* self, int note, float velocity);    // optional — NULL = ignores notes
    void (*note_off)     (void* self, int note);
    void (*all_notes_off)(void* self);
} AT_SourceType;
