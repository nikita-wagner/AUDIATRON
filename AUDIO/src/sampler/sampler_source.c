// ═══════════════════════════════════════════════════════════════════════════
//  sampler/sampler_source.c — lets the engine drive an AT_Sampler as a source
//
//  The only file in this folder that deals with the engine's source contract
//  (core/at_module.h). The DSP in sampler.c stays free of it.
// ═══════════════════════════════════════════════════════════════════════════

#include "sampler/sampler.h"

static void source_render  (AT_Buffer* out, void* self)           { sampler_render((AT_Sampler*)self, out); }
static void source_note_on (void* self, int note, float velocity) { sampler_note_on((AT_Sampler*)self, note, velocity); }
static void source_note_off(void* self, int note)                 { sampler_note_off((AT_Sampler*)self, note); }
static void source_all_off (void* self)                           { sampler_all_notes_off((AT_Sampler*)self); }

const AT_SourceType AT_SAMPLER_SOURCE = {
    .name          = "sampler",
    .render        = source_render,
    .note_on       = source_note_on,
    .note_off      = source_note_off,
    .all_notes_off = source_all_off
};
