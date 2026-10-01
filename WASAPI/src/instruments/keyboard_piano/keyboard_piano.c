#include "instruments/keyboard_piano/keyboard_piano.h"

#include <stdio.h>

void keyboard_piano_init(AT_KeyboardPiano* p, float sample_rate) {
    memset(p, 0, sizeof *p);
    synth_init(&p->synth, sample_rate);
    piano_keys_init(&p->keys, 4);
    p->wave = p->synth.wave;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void piano_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_KeyboardPiano* p = (AT_KeyboardPiano*)self;
    p->engine  = engine;
    p->channel = channel;

    keyboard_piano_load_patch(p);       // before the source is plugged in: no audio thread involved yet
    engine_set_source(engine, channel, &AT_SYNTH_SOURCE, &p->synth);
}

static bool piano_key(void* self, const AT_KeyEvent* ev) {
    return keyboard_piano_key((AT_KeyboardPiano*)self, ev);
}

static void piano_release_all(void* self) {
    keyboard_piano_release_all((AT_KeyboardPiano*)self);
}

static void piano_status(void* self, char* out, size_t cap) {
    const AT_KeyboardPiano* p = (const AT_KeyboardPiano*)self;
    snprintf(out, cap, "oct %d %s", p->keys.octave, oscillator_waveform_name(p->wave));
}

AT_Instrument keyboard_piano_instrument(AT_KeyboardPiano* p) {
    return (AT_Instrument){
        .name        = "piano",
        .help        = KEYBOARD_PIANO_HELP,
        .self        = p,
        .attach      = piano_attach,
        .key         = piano_key,
        .release_all = piano_release_all,
        .status      = piano_status
    };
}
