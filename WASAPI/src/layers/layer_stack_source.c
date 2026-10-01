// ═══════════════════════════════════════════════════════════════════════════
//  layers/layer_stack_source.c — lets the engine drive a layer stack as a source
//
//  The only file in this folder that deals with the engine's source contract
//  (core/at_module.h). Because a stack is itself a source, it can also be a
//  layer inside another stack.
// ═══════════════════════════════════════════════════════════════════════════

#include "layers/layer_stack.h"

static void source_render  (AT_Buffer* out, void* self)            { layer_stack_render((AT_LayerStack*)self, out); }
static void source_note_on (void* self, int note, float velocity)  { layer_stack_note_on((AT_LayerStack*)self, note, velocity); }
static void source_note_off(void* self, int note)                  { layer_stack_note_off((AT_LayerStack*)self, note); }
static void source_all_off (void* self)                            { layer_stack_all_notes_off((AT_LayerStack*)self); }

const AT_SourceType AT_LAYER_STACK_SOURCE = {
    .name          = "layers",
    .render        = source_render,
    .note_on       = source_note_on,
    .note_off      = source_note_off,
    .all_notes_off = source_all_off
};
