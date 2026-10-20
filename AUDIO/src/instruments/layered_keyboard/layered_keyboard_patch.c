// ═══════════════════════════════════════════════════════════════════════════
//  instruments/layered_keyboard/layered_keyboard_patch.c — ◄ THE INSTRUMENTS
//
//  A layered keyboard has AT_LAYERED_KEYS_MAX layer slots, each a synth with a
//  tremolo after it. Which instrument it is decides what every slot plays: the
//  table below, one instrument per entry, one row per layer (a row without a
//  name leaves its slot silent). To add an instrument, add an entry; to change
//  one, edit its numbers. Switching instruments only sets parameters, so it
//  works while playing, and everything stays adjustable afterwards.
//
//  How the acoustic ones are made (synth/synth.h):
//    guitar      a plucked string (Karplus–Strong), doubled a few cents apart,
//                with a short low thump for the body
//    violin      a saw through a resonant low-pass that bites at the start of
//                the stroke, a little scoop onto the note, vibrato that comes
//                in after a moment, a touch of bow noise
//    trumpet     the low-pass opens fast and far at the start (the brassy
//                "blat"), a clear scoop up onto the note, late vibrato, breath
//    contrabass  the violin's recipe an octave down, darker; or plucked
//  Slightly different tunings (detune) make layers drift against each other
//  instead of fusing into one static waveform: that is most of what makes a
//  layered sound feel "full".
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/layered_keyboard/layered_keyboard.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char*   name;             // NULL = the slot is silent in this instrument
    AT_Waveform   wave;
    AT_ADSRParams adsr;             // attack s, decay s, sustain level, release s
    float         detune_cents;     // 100 = one semitone
    int           transpose;        // semitones, +12 = one octave above
    float         volume;           // share of the composite sound, 0..2
    float         pan;              // -1 = left .. +1 = right
    float         tremolo_hz;       // gentle volume movement on this layer only; 0 = none
    float         tremolo_depth;
    float         gain;             // the synth's gain (0 = 0.2)
    float         filter_hz, filter_res, filter_env, filter_decay;
    float         vibrato_hz, vibrato_cents, vibrato_delay;
    float         bend_cents, bend_time;
    float         noise;
    float         pluck_bright, pluck_decay;
} LayerPatch;

typedef struct {
    const char* word;               // in project files: "instrument guitar"
    const char* name;               // shown
    LayerPatch  layers[AT_LAYERED_KEYS_MAX];
} KeysInstrument;

static const KeysInstrument k_instruments[] = {
    // the layered sound this keyboard always had: piano + strings + synth
    { "layered", "Layered (piano + strings + synth)", {
        { .name = "piano",   .wave = AT_WAVE_TRIANGLE, .adsr = { 0.002f, 0.70f, 0.12f, 0.35f },
          .volume = 1.00f, .pan = -0.15f },
        { .name = "strings", .wave = AT_WAVE_SAW,      .adsr = { 0.300f, 0.40f, 0.85f, 0.90f }, .detune_cents = 7.0f,
          .volume = 0.50f, .pan = 0.30f, .tremolo_hz = 5.0f, .tremolo_depth = 0.25f },
        { .name = "synth",   .wave = AT_WAVE_SQUARE,   .adsr = { 0.010f, 0.25f, 0.50f, 0.25f }, .detune_cents = -6.0f,
          .transpose = -12, .volume = 0.35f },
    } },
    { "piano", "Piano", {
        { .name = "hammer",  .wave = AT_WAVE_TRIANGLE, .adsr = { 0.001f, 1.60f, 0.0f, 0.45f },
          .volume = 1.00f, .pan = -0.10f, .gain = 0.25f,
          .filter_hz = 2400.0f, .filter_res = 0.10f, .filter_env = 1.8f, .filter_decay = 0.35f },
        { .name = "strings", .wave = AT_WAVE_SAW,      .adsr = { 0.001f, 2.20f, 0.0f, 0.50f }, .detune_cents = 3.0f,
          .volume = 0.35f, .pan = 0.15f,
          .filter_hz = 900.0f, .filter_env = 2.0f, .filter_decay = 0.25f },
        { .name = "body",    .wave = AT_WAVE_SINE,     .adsr = { 0.001f, 2.80f, 0.0f, 0.50f }, .detune_cents = -2.0f,
          .volume = 0.50f },
        { .name = "tine",    .wave = AT_WAVE_SINE,     .adsr = { 0.001f, 0.25f, 0.0f, 0.20f }, .transpose = 12,
          .volume = 0.15f },
    } },
    // the old Piano Synth track: one saw voice
    { "synth", "Synth", {
        { .name = "synth",   .wave = AT_WAVE_SAW,      .adsr = { 0.005f, 0.20f, 0.60f, 0.35f }, .volume = 1.00f },
    } },
    { "guitar", "Acoustic Guitar", {
        { .name = "string",  .wave = AT_WAVE_PLUCK,    .adsr = { 0.001f, 6.0f, 1.0f, 0.18f },
          .volume = 1.00f, .pan = -0.10f, .gain = 0.35f, .filter_hz = 5200.0f,
          .pluck_bright = 0.55f, .pluck_decay = 3.0f },
        { .name = "double",  .wave = AT_WAVE_PLUCK,    .adsr = { 0.001f, 6.0f, 1.0f, 0.18f }, .detune_cents = 5.0f,
          .volume = 0.45f, .pan = 0.25f, .gain = 0.35f, .filter_hz = 3500.0f,
          .pluck_bright = 0.35f, .pluck_decay = 2.4f },
        { .name = "body",    .wave = AT_WAVE_SINE,     .adsr = { 0.001f, 0.12f, 0.0f, 0.10f }, .transpose = -12,
          .volume = 0.25f },
    } },
    { "strings", "String Ensemble", {
        { .name = "violins", .wave = AT_WAVE_SAW,      .adsr = { 0.35f, 0.50f, 0.90f, 0.90f }, .detune_cents = -7.0f,
          .volume = 0.60f, .pan = -0.45f, .filter_hz = 3200.0f, .filter_res = 0.15f,
          .vibrato_hz = 5.2f, .vibrato_cents = 9.0f, .vibrato_delay = 0.30f, .noise = 0.02f },
        { .name = "violas",  .wave = AT_WAVE_SAW,      .adsr = { 0.35f, 0.50f, 0.90f, 0.90f }, .detune_cents = 8.0f,
          .volume = 0.60f, .pan = 0.45f, .filter_hz = 2600.0f, .filter_res = 0.15f,
          .vibrato_hz = 4.8f, .vibrato_cents = 10.0f, .vibrato_delay = 0.35f, .noise = 0.02f },
        { .name = "cellos",  .wave = AT_WAVE_SAW,      .adsr = { 0.35f, 0.50f, 0.90f, 1.00f }, .detune_cents = 2.0f,
          .transpose = -12, .volume = 0.45f, .filter_hz = 1500.0f, .filter_res = 0.10f,
          .vibrato_hz = 4.5f, .vibrato_cents = 8.0f, .vibrato_delay = 0.40f },
    } },
    { "violin", "Violin", {
        { .name = "bow",     .wave = AT_WAVE_SAW,      .adsr = { 0.07f, 0.30f, 0.85f, 0.28f },
          .volume = 1.00f, .gain = 0.25f,
          .filter_hz = 2800.0f, .filter_res = 0.35f, .filter_env = 0.9f, .filter_decay = 0.20f,
          .vibrato_hz = 5.6f, .vibrato_cents = 20.0f, .vibrato_delay = 0.22f,
          .bend_cents = -18.0f, .bend_time = 0.05f, .noise = 0.05f },
        { .name = "body",    .wave = AT_WAVE_SAW,      .adsr = { 0.09f, 0.30f, 0.80f, 0.30f }, .detune_cents = 4.0f,
          .volume = 0.35f, .pan = 0.20f,
          .filter_hz = 4200.0f, .filter_res = 0.20f,
          .vibrato_hz = 5.9f, .vibrato_cents = 14.0f, .vibrato_delay = 0.25f },
    } },
    { "contrabass", "Contrabass", {
        { .name = "bow",     .wave = AT_WAVE_SAW,      .adsr = { 0.12f, 0.30f, 0.90f, 0.35f }, .transpose = -12,
          .volume = 1.00f, .gain = 0.28f,
          .filter_hz = 700.0f, .filter_res = 0.25f, .filter_env = 0.8f, .filter_decay = 0.25f,
          .vibrato_hz = 4.6f, .vibrato_cents = 12.0f, .vibrato_delay = 0.35f,
          .bend_cents = -12.0f, .bend_time = 0.08f, .noise = 0.04f },
        { .name = "growl",   .wave = AT_WAVE_SQUARE,   .adsr = { 0.12f, 0.30f, 0.90f, 0.35f }, .detune_cents = -5.0f,
          .transpose = -12, .volume = 0.35f, .filter_hz = 450.0f, .filter_res = 0.10f },
    } },
    { "contrabass_pizz", "Contrabass Pizzicato", {
        { .name = "string",  .wave = AT_WAVE_PLUCK,    .adsr = { 0.001f, 3.0f, 1.0f, 0.12f }, .transpose = -12,
          .volume = 1.00f, .gain = 0.45f, .filter_hz = 1400.0f,
          .pluck_bright = 0.22f, .pluck_decay = 1.6f },
        { .name = "thump",   .wave = AT_WAVE_SINE,     .adsr = { 0.001f, 0.35f, 0.0f, 0.10f }, .transpose = -12,
          .volume = 0.45f },
    } },
    { "trumpet", "Trumpet", {
        { .name = "brass",   .wave = AT_WAVE_SAW,      .adsr = { 0.025f, 0.25f, 0.80f, 0.12f },
          .volume = 1.00f, .gain = 0.25f,
          .filter_hz = 700.0f, .filter_res = 0.30f, .filter_env = 2.4f, .filter_decay = 0.16f,
          .vibrato_hz = 5.4f, .vibrato_cents = 10.0f, .vibrato_delay = 0.40f,
          .bend_cents = -70.0f, .bend_time = 0.035f, .noise = 0.04f },
        { .name = "edge",    .wave = AT_WAVE_SQUARE,   .adsr = { 0.030f, 0.25f, 0.75f, 0.12f }, .detune_cents = 4.0f,
          .volume = 0.30f, .pan = 0.10f,
          .filter_hz = 1100.0f, .filter_res = 0.20f, .filter_env = 1.8f, .filter_decay = 0.14f,
          .bend_cents = -70.0f, .bend_time = 0.035f },
    } },
};

#define INSTRUMENT_COUNT ((int)(sizeof k_instruments / sizeof *k_instruments))

int layered_keyboard_instrument_count(void) { return INSTRUMENT_COUNT; }

const char* layered_keyboard_instrument_name(int i) {
    return i >= 0 && i < INSTRUMENT_COUNT ? k_instruments[i].name : "?";
}

const char* layered_keyboard_instrument_word(int i) {
    return i >= 0 && i < INSTRUMENT_COUNT ? k_instruments[i].word : "?";
}

int layered_keyboard_instrument_find(const char* word) {
    for (int i = 0; word && i < INSTRUMENT_COUNT; i++)
        if (!strcmp(word, k_instruments[i].word)) return i;
    return -1;
}

// Before attach the settings are written directly; afterwards through the engine's queue.
static void set(AT_LayeredKeyboard* k, AT_ParamFn fn, void* self, int param, float value) {
    if (k->engine) engine_set_param(k->engine, fn, self, param, value);
    else           fn(self, param, value);
}

static void build_help(AT_LayeredKeyboard* k) {
    int used = snprintf(k->help, sizeof k->help,
                        PIANO_KEYS_HELP "  Y / X  octave -/+      %s: 1..%u  layer on/off (",
                        k_instruments[k->instrument].name, k->layer_count);
    bool first = true;
    for (uint32_t i = 0; i < k->layer_count && used > 0 && (size_t)used < sizeof k->help; i++) {
        if (!k->layer_name[i]) continue;
        used += snprintf(k->help + used, sizeof k->help - (size_t)used, "%s%s", first ? "" : " ", k->layer_name[i]);
        first = false;
    }
    if (used > 0 && (size_t)used < sizeof k->help)
        snprintf(k->help + used, sizeof k->help - (size_t)used, ")\n");
}

void layered_keyboard_set_instrument(AT_LayeredKeyboard* k, int index) {
    if (index < 0 || index >= INSTRUMENT_COUNT) return;
    const KeysInstrument* ins = &k_instruments[index];
    k->instrument = index;

    for (uint32_t i = 0; i < k->layer_count; i++) {
        const LayerPatch* p    = &ins->layers[i];
        const bool        used = p->name != NULL;
        if (used) {
            AT_Synth* s = &k->synths[i];
            set(k, synth_set_param, s, AT_SYNTH_PARAM_WAVEFORM,      (float)p->wave);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_ATTACK,        p->adsr.attack_s);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_DECAY,         p->adsr.decay_s);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_SUSTAIN,       p->adsr.sustain);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_RELEASE,       p->adsr.release_s);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_GAIN,          p->gain > 0.0f ? p->gain : 0.2f);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_DETUNE,        p->detune_cents);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_FILTER,        p->filter_hz);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_RESONANCE,     p->filter_res);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_FILTER_ENV,    p->filter_env);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_FILTER_DECAY,  p->filter_decay > 0.0f ? p->filter_decay : 0.3f);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_VIBRATO_RATE,  p->vibrato_hz);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_VIBRATO_DEPTH, p->vibrato_cents);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_VIBRATO_DELAY, p->vibrato_delay);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_BEND,          p->bend_cents);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_BEND_TIME,     p->bend_time > 0.0f ? p->bend_time : 0.05f);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_NOISE,         p->noise);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_PLUCK_BRIGHT,  p->pluck_bright > 0.0f ? p->pluck_bright : 0.5f);
            set(k, synth_set_param, s, AT_SYNTH_PARAM_PLUCK_DECAY,   p->pluck_decay > 0.0f ? p->pluck_decay : 3.0f);
            set(k, layer_stack_set_param, &k->stack, AT_LAYER_PARAM(i, AT_LAYER_PARAM_VOLUME),    p->volume);
            set(k, layer_stack_set_param, &k->stack, AT_LAYER_PARAM(i, AT_LAYER_PARAM_PAN),       p->pan);
            set(k, layer_stack_set_param, &k->stack, AT_LAYER_PARAM(i, AT_LAYER_PARAM_TRANSPOSE), (float)p->transpose);
            set(k, tremolo_set_param, &k->motion[i], AT_TREMOLO_PARAM_RATE,  p->tremolo_hz > 0.0f ? p->tremolo_hz : 5.0f);
            set(k, tremolo_set_param, &k->motion[i], AT_TREMOLO_PARAM_DEPTH, p->tremolo_hz > 0.0f ? p->tremolo_depth : 0.0f);
        }
        k->layer_name[i]       = used ? p->name : NULL;
        k->stack.layers[i].name = used ? p->name : "-";
        k->layer_on[i]         = used;
        set(k, layer_stack_set_param, &k->stack, AT_LAYER_PARAM(i, AT_LAYER_PARAM_MUTE), used ? 0.0f : 1.0f);
    }
    build_help(k);
}

// Called from init: every slot a synth with a tremolo after it, then the first instrument.
void layered_keyboard_build_layers(AT_LayeredKeyboard* k, float sample_rate) {
    layer_stack_init(&k->stack, sample_rate);
    k->layer_count = 0;
    for (uint32_t i = 0; i < AT_LAYERED_KEYS_MAX; i++) {
        AT_Synth* synth = &k->synths[i];
        synth_init(synth, sample_rate);
        AT_Layer* layer = layer_stack_add(&k->stack, "-", &AT_SYNTH_SOURCE, synth);
        if (!layer) break;
        tremolo_init(&k->motion[i], 5.0f, 0.0f);
        layer_stack_set_insert(&k->stack, i, 0, tremolo_process, &k->motion[i]);
        k->layer_count++;
    }
    layered_keyboard_set_instrument(k, 0);
}

// Called from attach: the EQ of the track the whole composite sound runs through.
void layered_keyboard_setup_channel(AT_LayeredKeyboard* k) {
    engine_set_eq_band(k->engine, (int)k->channel, 0, AT_FILTER_HIGHPASS, 40.0f,   0.707f, 0.0f);
    engine_set_eq_band(k->engine, (int)k->channel, 1, AT_FILTER_LOWPASS,  9000.0f, 0.707f, 0.0f);
}
