#include "layers/layer_stack.h"

#include <limits.h>

#include "core/at_math.h"

// ─── Setup (before the stack is plugged in) ──────────────────────────────────

void layer_stack_init(AT_LayerStack* s, float sample_rate) {
    memset(s, 0, sizeof *s);
    s->sample_rate = sample_rate;
}

AT_Layer* layer_stack_add(AT_LayerStack* s, const char* name, const AT_SourceType* type, void* self) {
    if (s->count == AT_LAYER_MAX || !type || !type->render) return NULL;

    AT_Layer* l = &s->layers[s->count++];
    memset(l, 0, sizeof *l);
    l->name = name;
    l->type = type;
    l->self = self;
    l->note_low  = INT_MIN;
    l->note_high = INT_MAX;
    channel_strip_init(&l->strip, s->sample_rate);
    l->strip.volume = 1.0f;         // a stack with one untouched layer sounds like the bare source
    return l;
}

bool layer_stack_set_range(AT_LayerStack* s, uint32_t layer, int low, int high) {
    if (layer >= s->count || low > high) return false;
    s->layers[layer].note_low  = low;
    s->layers[layer].note_high = high;
    return true;
}

bool layer_stack_set_insert(AT_LayerStack* s, uint32_t layer, uint32_t slot, AT_ProcessFn fn, void* user) {
    if (layer >= s->count || slot >= AT_LAYER_INSERTS) return false;
    s->layers[layer].inserts[slot] = (AT_LayerInsert){ fn, user };
    return true;
}

// ─── Notes: every layer gets every note in its key range ─────────────────────

static bool in_range(const AT_Layer* l, int note) {
    return note >= l->note_low && note <= l->note_high;
}

void layer_stack_note_on(AT_LayerStack* s, int note, float velocity) {
    for (uint32_t i = 0; i < s->count; i++) {
        AT_Layer* l = &s->layers[i];
        if (l->type->note_on && in_range(l, note)) l->type->note_on(l->self, note + l->transpose, velocity);
    }
}

void layer_stack_note_off(AT_LayerStack* s, int note) {
    for (uint32_t i = 0; i < s->count; i++) {
        AT_Layer* l = &s->layers[i];
        if (l->type->note_off && in_range(l, note)) l->type->note_off(l->self, note + l->transpose);
    }
}

static void layer_all_notes_off(AT_Layer* l) {
    if (l->type->all_notes_off) l->type->all_notes_off(l->self);
}

void layer_stack_all_notes_off(AT_LayerStack* s) {
    for (uint32_t i = 0; i < s->count; i++) layer_all_notes_off(&s->layers[i]);
}

// ─── Render: source → inserts → volume/pan → (+=) out ────────────────────────

void layer_stack_render(AT_LayerStack* s, AT_Buffer* out) {
    AT_Buffer scratch = *out;       // same length, rate and clock as the output
    for (uint32_t c = 0; c < scratch.channels && c < AT_MAX_CHANNELS; c++) scratch.ch[c] = s->scratch[c];

    for (uint32_t i = 0; i < s->count; i++) {
        AT_Layer* l = &s->layers[i];

        // A muted layer is still rendered, so its envelopes keep following the
        // notes and it comes back mid-chord the moment it is unmuted.
        at_buffer_clear(&scratch);
        l->type->render(&scratch, l->self);

        for (int k = 0; k < AT_LAYER_INSERTS; k++)
            if (l->inserts[k].fn) l->inserts[k].fn(&scratch, l->inserts[k].user);

        channel_strip_mix(&l->strip, &scratch, out);
    }
}

// ─── Parameters (audio thread, between two blocks) ───────────────────────────

void layer_stack_set_param(void* self, int param, float value) {
    AT_LayerStack* s     = (AT_LayerStack*)self;
    uint32_t       index = (uint32_t)param >> 8;
    if (index >= s->count) return;
    AT_Layer* l = &s->layers[index];

    switch ((AT_LayerParam)(param & 0xFF)) {
    case AT_LAYER_PARAM_VOLUME: l->strip.volume = at_clampf(value, 0.0f, 2.0f);  break;
    case AT_LAYER_PARAM_PAN:    l->strip.pan    = at_clampf(value, -1.0f, 1.0f); break;
    case AT_LAYER_PARAM_MUTE:   l->strip.mute   = value != 0.0f;                 break;
    case AT_LAYER_PARAM_TRANSPOSE:
        // The notes sounding now were started at the old offset; their
        // note-offs would no longer reach them.
        layer_all_notes_off(l);
        l->transpose = (int)value;
        break;
    }
}

float layer_stack_peak(const AT_LayerStack* s, uint32_t layer) {
    return layer < s->count ? s->layers[layer].strip.peak : 0.0f;
}
