// ═══════════════════════════════════════════════════════════════════════════
//  instruments/layered_keyboard/layered_keyboard_patch.c — ◄ THE LAYERS
//
//  The sound of this instrument is the table below: one row = one layer.
//  To add a layer, add a row (up to AT_LAYER_MAX); to change the blend, edit
//  the numbers. Key handling is in layered_keyboard_keys.c and stays untouched.
//
//  Why the layers differ the way they do:
//    piano     fast attack, quick decay    → the "hit" at the start of a note
//    strings   slow attack, long release   → swells in behind it and lingers
//    synth     one octave below            → body underneath
//  Slightly different tunings (detune) make the layers drift against each
//  other instead of fusing into one static waveform; that is most of what
//  makes a layered sound feel "full".
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/layered_keyboard/layered_keyboard.h"

#include <stdio.h>

typedef struct {
    const char*   name;
    AT_Waveform   wave;
    AT_ADSRParams adsr;             // attack s, decay s, sustain level, release s
    float         detune_cents;     // 100 = one semitone
    int           transpose;        // semitones, +12 = one octave above
    float         volume;           // share of the composite sound, 0..2
    float         pan;              // -1 = left .. +1 = right
    float         tremolo_hz;       // gentle volume movement on this layer only; 0 = none
    float         tremolo_depth;
} LayerPatch;

static const LayerPatch k_layers[] = {
//    name        waveform            attack  decay  sustain release   detune  transp  volume   pan    trem Hz  depth
    { "piano",    AT_WAVE_TRIANGLE, { 0.002f, 0.70f, 0.12f,  0.35f },   0.0f,    0,    1.00f, -0.15f,  0.0f,   0.00f },
    { "strings",  AT_WAVE_SAW,      { 0.300f, 0.40f, 0.85f,  0.90f },   7.0f,    0,    0.50f,  0.30f,  5.0f,   0.25f },
    { "synth",    AT_WAVE_SQUARE,   { 0.010f, 0.25f, 0.50f,  0.25f },  -6.0f,  -12,    0.35f,  0.00f,  0.0f,   0.00f },
};

#define LAYER_COUNT (sizeof k_layers / sizeof *k_layers)
_Static_assert(LAYER_COUNT <= AT_LAYER_MAX, "too many rows in k_layers");

// Called from init: nothing is plugged into the engine yet, so the synths and
// the stack can still be written directly.
void layered_keyboard_build_layers(AT_LayeredKeyboard* k, float sample_rate) {
    layer_stack_init(&k->stack, sample_rate);
    k->layer_count = 0;

    for (uint32_t i = 0; i < LAYER_COUNT; i++) {
        const LayerPatch* p = &k_layers[i];

        AT_Synth* synth = &k->synths[i];
        synth_init(synth, sample_rate);
        synth->wave         = p->wave;
        synth->adsr         = p->adsr;
        synth->detune_cents = p->detune_cents;

        AT_Layer* layer = layer_stack_add(&k->stack, p->name, &AT_SYNTH_SOURCE, synth);
        layer->transpose    = p->transpose;
        layer->strip.volume = p->volume;
        layer->strip.pan    = p->pan;

        if (p->tremolo_hz > 0.0f) {
            tremolo_init(&k->motion[i], p->tremolo_hz, p->tremolo_depth);
            layer_stack_set_insert(&k->stack, i, 0, tremolo_process, &k->motion[i]);
        }

        k->layer_name[i] = p->name;
        k->layer_on[i]   = true;
        k->layer_count++;
    }

    // help text: the layout line + this instrument's own keys
    int used = snprintf(k->help, sizeof k->help,
                        PIANO_KEYS_HELP "  Y / X  octave -/+      1..%u  layer on/off (", k->layer_count);
    for (uint32_t i = 0; i < k->layer_count && used > 0 && (size_t)used < sizeof k->help; i++)
        used += snprintf(k->help + used, sizeof k->help - (size_t)used, "%s%s", i ? " " : "", k->layer_name[i]);
    if (used > 0 && (size_t)used < sizeof k->help)
        snprintf(k->help + used, sizeof k->help - (size_t)used, ")\n");
}

// Called from attach: the EQ of the track the whole composite sound runs through.
void layered_keyboard_setup_channel(AT_LayeredKeyboard* k) {
    engine_set_eq_band(k->engine, (int)k->channel, 0, AT_FILTER_HIGHPASS, 40.0f,   0.707f, 0.0f);
    engine_set_eq_band(k->engine, (int)k->channel, 1, AT_FILTER_LOWPASS,  9000.0f, 0.707f, 0.0f);
}
