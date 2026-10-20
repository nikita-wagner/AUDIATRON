#include "instruments/layered_keyboard/layered_keyboard.h"

#include <stdio.h>

void layered_keyboard_init(AT_LayeredKeyboard* k, float sample_rate) {
    memset(k, 0, sizeof *k);
    piano_keys_init(&k->keys, 4);
    layered_keyboard_build_layers(k, sample_rate);
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void layered_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_LayeredKeyboard* k = (AT_LayeredKeyboard*)self;
    k->engine  = engine;
    k->channel = channel;

    layered_keyboard_setup_channel(k);
    engine_set_source(engine, channel, &AT_LAYER_STACK_SOURCE, &k->stack);
}

static bool layered_key(void* self, const AT_KeyEvent* ev) {
    return layered_keyboard_key((AT_LayeredKeyboard*)self, ev);
}

static void layered_release_all(void* self) {
    layered_keyboard_release_all((AT_LayeredKeyboard*)self);
}

// e.g. "oct 4 piano+strings+synth"
static void layered_status(void* self, char* out, size_t cap) {
    const AT_LayeredKeyboard* k = (const AT_LayeredKeyboard*)self;

    int  used = snprintf(out, cap, "oct %d ", k->keys.octave);
    bool any  = false;
    for (uint32_t i = 0; i < k->layer_count; i++) {
        if (!k->layer_on[i] || !k->layer_name[i]) continue;
        if (used < 0 || (size_t)used >= cap) return;        // full
        used += snprintf(out + used, cap - (size_t)used, "%s%s", any ? "+" : "", k->layer_name[i]);
        any = true;
    }
    if (!any && used > 0 && (size_t)used < cap)
        snprintf(out + used, cap - (size_t)used, "(all layers off)");
}

AT_Instrument layered_keyboard_instrument(AT_LayeredKeyboard* k) {
    return (AT_Instrument){
        .name        = "layers",
        .help        = k->help,
        .self        = k,
        .attach      = layered_attach,
        .key         = layered_key,
        .release_all = layered_release_all,
        .status      = layered_status
    };
}
