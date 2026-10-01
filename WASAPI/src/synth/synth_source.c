// ═══════════════════════════════════════════════════════════════════════════
//  synth/synth_source.c — lets the engine drive an AT_Synth as a source
//
//  This is the only file in the synth folder that deals with the engine's
//  source contract (core/at_module.h). The DSP in synth.c stays free of it.
// ═══════════════════════════════════════════════════════════════════════════

#include "synth/synth.h"

// The synth is mono; copy it to every channel of the strip. The mixer's
// channel strip then places it in the stereo field.
static void source_render(AT_Buffer* out, void* self) {
    synth_render_mono((AT_Synth*)self, out->ch[0], out->frames);
    for (uint32_t c = 1; c < out->channels; c++)
        memcpy(out->ch[c], out->ch[0], out->frames * sizeof(float));
}

static void source_note_on (void* self, int note, float velocity) { synth_note_on((AT_Synth*)self, note, velocity); }
static void source_note_off(void* self, int note)                 { synth_note_off((AT_Synth*)self, note); }
static void source_all_off (void* self)                           { synth_all_notes_off((AT_Synth*)self); }

const AT_SourceType AT_SYNTH_SOURCE = {
    .name          = "synth",
    .render        = source_render,
    .note_on       = source_note_on,
    .note_off      = source_note_off,
    .all_notes_off = source_all_off
};
