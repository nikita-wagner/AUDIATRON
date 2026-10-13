#include "instruments/sample_keyboard/sample_keyboard.h"

#include <stdio.h>
#include <string.h>

#include "io/audio_file/audio_file.h"

bool sample_keyboard_init(AT_SampleKeyboard* k, const char* path, float sample_rate, char* error, size_t error_cap) {
    memset(k, 0, sizeof *k);

    if (!audio_file_load(path, &k->clip, error, error_cap)) return false;
    k->owns_clip = true;

    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(k->label, sizeof k->label, "%s", name);

    sampler_init(&k->sampler, sample_rate, &k->clip);
    piano_keys_init(&k->keys, 4);
    return true;
}

void sample_keyboard_init_copy(AT_SampleKeyboard* k, const AT_SampleKeyboard* original, float sample_rate) {
    memset(k, 0, sizeof *k);
    k->clip      = original->clip;                          // the same samples, not owned
    k->owns_clip = false;
    snprintf(k->label, sizeof k->label, "%s", original->label);
    sampler_init(&k->sampler, sample_rate, &k->clip);
    piano_keys_init(&k->keys, 4);
}

void sample_keyboard_free(AT_SampleKeyboard* k) {
    if (k->owns_clip) audio_clip_free(&k->clip);
    memset(&k->clip, 0, sizeof k->clip);
    memset(&k->sampler, 0, sizeof k->sampler);              // its clip pointer is gone
}

bool sample_keyboard_set_cut(AT_SampleKeyboard* k, float from, float to) {
    if (!(from >= 0.0f && from < to && to <= 1.0f)) return false;      // also rejects NaN

    if (!k->engine) { sampler_set_region(&k->sampler, from, to); return true; }    // not plugged in yet
    engine_set_param(k->engine, sampler_set_param, &k->sampler, AT_SAMPLER_PARAM_REGION_FROM, from);
    engine_set_param(k->engine, sampler_set_param, &k->sampler, AT_SAMPLER_PARAM_REGION_TO,   to);
    return true;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void keyboard_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_SampleKeyboard* k = (AT_SampleKeyboard*)self;
    k->engine  = engine;
    k->channel = channel;

    engine_set_source(engine, channel, &AT_SAMPLER_SOURCE, &k->sampler);
    engine_set_eq_band(engine, (int)channel, 0, AT_FILTER_HIGHPASS, 25.0f, 0.707f, 0.0f);     // rumble below a kick
}

static bool keyboard_key(void* self, const AT_KeyEvent* ev) {
    return sample_keyboard_key((AT_SampleKeyboard*)self, ev);
}

static void keyboard_release_all(void* self) {
    sample_keyboard_release_all((AT_SampleKeyboard*)self);
}

// e.g. "kick.mp3 oct 4 one-shot"
static void keyboard_status(void* self, char* out, size_t cap) {
    const AT_SampleKeyboard* k = (const AT_SampleKeyboard*)self;

    char name[16];
    snprintf(name, sizeof name, "%.14s", k->label);         // long names are cut to fit the status line
    snprintf(out, cap, "%s oct %d %s%s", name, k->keys.octave, k->gate ? "gated" : "one-shot", k->loop ? "+loop" : "");
}

AT_Instrument sample_keyboard_instrument(AT_SampleKeyboard* k) {
    return (AT_Instrument){
        .name        = "sampler",
        .help        = SAMPLE_KEYBOARD_HELP,
        .self        = k,
        .attach      = keyboard_attach,
        .key         = keyboard_key,
        .release_all = keyboard_release_all,
        .status      = keyboard_status
    };
}
