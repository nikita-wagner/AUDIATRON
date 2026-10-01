#include "instruments/_template/template_instrument.h"

#include <stdio.h>

#define VELOCITY 0.9f

// ─── Playing: which key triggers which note ──────────────────────────────────

typedef struct { uint16_t scan; int note; } Pad;

static const Pad k_pads[] = {
    { AT_SC_A, 48 },    // C3
    { AT_SC_S, 52 },    // E3
    { AT_SC_D, 55 },    // G3
    { AT_SC_F, 60 },    // C4
};

static const Pad* find_pad(uint16_t scan) {
    for (size_t i = 0; i < sizeof k_pads / sizeof *k_pads; i++)
        if (k_pads[i].scan == scan) return &k_pads[i];
    return NULL;
}

static bool template_key(void* self, const AT_KeyEvent* ev) {
    AT_TemplateInstrument* t = (AT_TemplateInstrument*)self;

    const Pad* pad = find_pad(ev->scan_code);
    if (!pad) return false;                         // not ours — the app may use it

    if (ev->down && !ev->repeat) {
        t->held[ev->scan_code] = true;
        engine_note_on(t->engine, t->channel, pad->note, VELOCITY);
    } else if (!ev->down) {
        t->held[ev->scan_code] = false;
        engine_note_off(t->engine, t->channel, pad->note);
    }
    return true;
}

static void template_release_all(void* self) {
    AT_TemplateInstrument* t = (AT_TemplateInstrument*)self;

    for (size_t i = 0; i < sizeof k_pads / sizeof *k_pads; i++) {
        if (!t->held[k_pads[i].scan]) continue;
        t->held[k_pads[i].scan] = false;
        engine_note_off(t->engine, t->channel, k_pads[i].note);
    }
}

// ─── Sound: source settings + mixer channel ──────────────────────────────────

static void template_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_TemplateInstrument* t = (AT_TemplateInstrument*)self;
    t->engine  = engine;
    t->channel = channel;

    // Not plugged in yet, so the synth may still be written directly.
    t->synth.wave = AT_WAVE_SQUARE;
    t->synth.adsr = (AT_ADSRParams){ .attack_s = 0.002f, .decay_s = 0.15f, .sustain = 0.0f, .release_s = 0.1f };

    engine_set_source(engine, channel, &AT_SYNTH_SOURCE, &t->synth);
    engine_set_eq_band(engine, (int)channel, 0, AT_FILTER_LOWPASS, 3000.0f, 0.707f, 0.0f);
}

// ─── Status line + descriptor ────────────────────────────────────────────────

static void template_status(void* self, char* out, size_t cap) {
    (void)self;
    snprintf(out, cap, "4 pads");
}

void template_instrument_init(AT_TemplateInstrument* t, float sample_rate) {
    memset(t, 0, sizeof *t);
    synth_init(&t->synth, sample_rate);
}

AT_Instrument template_instrument_instrument(AT_TemplateInstrument* t) {
    return (AT_Instrument){
        .name        = "template",
        .help        = "  A S D F   pads\n",
        .self        = t,
        .attach      = template_attach,
        .key         = template_key,
        .release_all = template_release_all,
        .status      = template_status
    };
}
