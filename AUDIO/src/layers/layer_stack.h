// ═══════════════════════════════════════════════════════════════════════════
//  layers/layer_stack.h — several sounds played together as ONE sound
//
//  A layer stack is a source made of other sources. Every note it receives
//  goes to all of its layers, and their outputs are mixed into one signal. To
//  the engine it is a single source on a single track, so the whole composite
//  sound shares one channel EQ, one fader and one set of track effects.
//
//  One layer = a source (anything with an AT_SourceType: a synth, later a
//  sampler, even another layer stack) + how it sits in the composite sound:
//
//      key range   the notes the layer answers (default: all). One note per
//                  layer turns a stack into a drum kit: every pad its own sound
//      transpose   semitones added to every note (+12 = one octave above)
//      strip       volume, pan, mute — smoothed, with a peak meter
//      inserts     effects that change this layer only
//
//  Layers vs. tracks: layers answer the SAME notes and form one instrument.
//  Tracks (tracks/track_list.h) are separate mixer channels with different
//  instruments, played independently.
//
//  Files in this folder:
//    layer_stack.c          setup, notes, rendering, parameters
//    layer_stack_source.c   adapter that lets the engine use a stack as a source
//
//  Threading: build the stack (layer_stack_add, layer_stack_set_insert, direct
//  writes to the returned AT_Layer) BEFORE plugging it into the engine.
//  Afterwards everything here is audio-thread only. From the control thread:
//      engine_note_on / engine_note_off                              for notes
//      engine_set_param(e, layer_stack_set_param, &stack,
//                       AT_LAYER_PARAM(layer, id), value)            for parameters
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"
#include "core/at_module.h"
#include "mixer/channel_strip/channel_strip.h"

#define AT_LAYER_MAX      256       // layers per stack (a drumpad: one per pad, up to 16 x 16)
#define AT_LAYER_INSERTS  2         // chained effects per layer

typedef struct {
    AT_ProcessFn fn;                // NULL = empty slot
    void*        user;
} AT_LayerInsert;

typedef struct {
    const char*          name;
    const AT_SourceType* type;      // the layer's sound generator
    void*                self;
    int                  note_low;  // key range: the layer only answers notes
    int                  note_high; //   note_low..note_high (before transpose)
    int                  transpose; // semitones added to every note
    AT_ChannelStrip      strip;     // volume / pan / mute inside the composite sound
    AT_LayerInsert       inserts[AT_LAYER_INSERTS];
} AT_Layer;

typedef struct {
    AT_Layer layers[AT_LAYER_MAX];
    uint32_t count;
    float    sample_rate;
    float    scratch[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];     // one layer is rendered here, then mixed
} AT_LayerStack;

typedef enum {
    AT_LAYER_PARAM_VOLUME,          // linear, 0..2
    AT_LAYER_PARAM_PAN,             // -1 = left .. +1 = right
    AT_LAYER_PARAM_MUTE,            // 0 = audible, 1 = muted (the layer keeps following the notes)
    AT_LAYER_PARAM_TRANSPOSE        // semitones; stops the layer's sounding notes first
} AT_LayerParam;

// The `param` argument of layer_stack_set_param: which layer + which setting.
#define AT_LAYER_PARAM(layer, id)  ((int)(((layer) << 8) | (id)))

void layer_stack_init(AT_LayerStack* s, float sample_rate);

// Appends a layer at full volume, centered, not transposed. Returns it so the
// caller can adjust transpose / strip.volume / strip.pan, or NULL if the stack
// is full. `self` (the layer's source state) must outlive the stack's use.
AT_Layer* layer_stack_add(AT_LayerStack* s, const char* name, const AT_SourceType* type, void* self);

// Limits a layer to the notes low..high (inclusive, the notes the stack
// receives, before transpose). A new layer answers every note.
bool layer_stack_set_range(AT_LayerStack* s, uint32_t layer, int low, int high);

// Puts an effect on one layer only. fn = NULL empties the slot.
bool layer_stack_set_insert(AT_LayerStack* s, uint32_t layer, uint32_t slot, AT_ProcessFn fn, void* user);

void layer_stack_note_on      (AT_LayerStack* s, int note, float velocity);
void layer_stack_note_off     (AT_LayerStack* s, int note);
void layer_stack_all_notes_off(AT_LayerStack* s);

// Mixes every layer into `out` (out += layers). `out` must be stereo and at
// most AT_MAX_BLOCK_FRAMES long.
void layer_stack_render(AT_LayerStack* s, AT_Buffer* out);

// AT_ParamFn — self = AT_LayerStack*, param = AT_LAYER_PARAM(layer, AT_LayerParam).
void layer_stack_set_param(void* self, int param, float value);

// Post-fader peak of one layer during the last block (any thread, approximate).
float layer_stack_peak(const AT_LayerStack* s, uint32_t layer);

// Plug a stack into an engine channel:
//     engine_set_source(&engine, channel, &AT_LAYER_STACK_SOURCE, &my_stack);
extern const AT_SourceType AT_LAYER_STACK_SOURCE;
