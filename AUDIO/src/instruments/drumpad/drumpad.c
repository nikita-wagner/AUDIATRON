#include "instruments/drumpad/drumpad.h"

#include <stdio.h>
#include <string.h>

#include "input/key_names/key_names.h"
#include "io/audio_file/audio_file.h"

// ─── Load / free ─────────────────────────────────────────────────────────────

void drumpad_init(AT_Drumpad* d, float sample_rate) {
    memset(d, 0, sizeof *d);
    d->sample_rate = sample_rate;
    d->selected    = -1;
    d->edit_pad    = -1;
    d->pat_stage   = -1;
    layer_stack_init(&d->stack, sample_rate);

    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        d->pads[i].layer  = -1;
        d->pads[i].volume = 1.0f;
        d->pads[i].clock  = &d->clock;
        d->pads[i].sync   = d->pads[i].synced = true;       // on the shared clock until set free
    }
}

static void make_label(AT_DrumPad* p, const char* path) {
    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(p->label, sizeof p->label, "%s", name);
}

bool drumpad_load(AT_Drumpad* d, int pad, const char* path, char* error, size_t error_cap) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS) {
        audio_clip_set_error(error, error_cap, "no pad %d (there are %d)", pad + 1, AT_DRUMPAD_PADS);
        return false;
    }
    if (d->engine || d->prepared) {
        audio_clip_set_error(error, error_cap, "pads can only be loaded before the drumpad is attached / prepared");
        return false;
    }

    AT_DrumPad* p = &d->pads[pad];
    AT_Clip     clip;
    if (!audio_file_load(path, &clip, error, error_cap)) return false;

    if (p->loaded) audio_clip_free(&p->clip);           // replaces the pad's earlier file
    p->clip   = clip;
    p->loaded = true;
    make_label(p, path);
    snprintf(p->path, sizeof p->path, "%s", path);

    sampler_init(&p->sampler, d->sample_rate, &p->clip);
    pitch_shift_init(&p->shifter, d->sample_rate);
    drumpad_fx_free(p);                                     // a pad loaded twice: drop the old echo
    drumpad_fx_init(p, d->sample_rate);
    p->cut_from  = 0.0f;
    p->cut_to    = 1.0f;
    p->cut_in_ms = false;
    p->wait_ms   = 0.0f;
    p->play_ms   = 0.0f;
    if (d->selected < 0) d->selected = pad;
    return true;
}

void drumpad_init_copy(AT_Drumpad* copy, const AT_Drumpad* original, float sample_rate) {
    drumpad_init(copy, sample_rate);
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        const AT_DrumPad* from = &original->pads[i];
        AT_DrumPad*       p    = &copy->pads[i];
        if (!from->loaded) continue;
        p->clip   = from->clip;                             // the same samples
        p->shared = true;
        p->loaded = true;
        snprintf(p->label, sizeof p->label, "%s", from->label);
        snprintf(p->path, sizeof p->path, "%s", from->path);
        sampler_init(&p->sampler, sample_rate, &p->clip);
        pitch_shift_init(&p->shifter, sample_rate);
        drumpad_fx_init(p, sample_rate);
        p->cut_from = 0.0f;
        p->cut_to   = 1.0f;
        if (copy->selected < 0) copy->selected = i;
    }
}

void drumpad_free(AT_Drumpad* d) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        AT_DrumPad* p = &d->pads[i];
        if (!p->loaded) continue;
        if (!p->shared) audio_clip_free(&p->clip);
        memset(&p->clip, 0, sizeof p->clip);
        p->shared = false;
        drumpad_fx_free(p);
        memset(&p->sampler, 0, sizeof p->sampler);      // its clip pointer is gone
        p->loaded = false;
    }
}

int drumpad_loaded_count(const AT_Drumpad* d) {
    int n = 0;
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) n += d->pads[i].loaded;
    return n;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

// Every loaded pad becomes one layer that answers only its own note. The layer
// transposes that note to the sampler's root, so a hit plays the file unchanged
// and the pad's pitch comes from the sampler rate alone.
static void build_stack(AT_Drumpad* d) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        AT_DrumPad* p = &d->pads[i];
        if (!p->loaded) continue;

        AT_Layer* layer = layer_stack_add(&d->stack, p->label, &AT_SAMPLER_SOURCE, &p->sampler);
        if (!layer) break;                                  // cannot happen: a layer per pad is asserted

        p->layer = (int)(d->stack.count - 1);
        int note = AT_DRUMPAD_FIRST_NOTE + i;
        layer_stack_set_range(&d->stack, (uint32_t)p->layer, note, note);
        layer->transpose = AT_SAMPLER_ROOT - note;
        layer_stack_set_insert(&d->stack, (uint32_t)p->layer, 0, pitch_shift_process, &p->shifter);
        layer_stack_set_insert(&d->stack, (uint32_t)p->layer, 1, drumpad_fx_process, p);     // the pad's own effects

        drumpad_apply_all(d, i);                            // engine still NULL: written directly
    }
}

void drumpad_prepare(AT_Drumpad* d) {
    if (d->prepared) return;
    build_stack(d);
    drumpad_build_help(d);
    d->prepared = true;
}

static void drumpad_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_Drumpad* d = (AT_Drumpad*)self;

    drumpad_prepare(d);

    engine_set_source(engine, channel, &AT_DRUMPAD_SOURCE, d);              // the stack + the clock
    engine_set_eq_band(engine, (int)channel, 0, AT_FILTER_HIGHPASS, 25.0f, 0.707f, 0.0f);     // rumble below a kick

    d->engine  = engine;
    d->channel = channel;
}

static bool pad_key(void* self, const AT_KeyEvent* ev) {
    return drumpad_key((AT_Drumpad*)self, ev);
}

static void pad_release_all(void* self) {
    drumpad_release_all((AT_Drumpad*)self);
}

// The offline demo: every loaded pad loops, each at its own settings.
static void drumpad_demo_start(void* self) {
    AT_Drumpad* d = (AT_Drumpad*)self;
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) drumpad_loop_start(d, i);
}

// e.g. "A kick.wav +3st loop sync v1.0 fx:ce cut | 2 loops"
// (tempo = keep-tempo pitch; sync / free = on the shared clock or its own;
//  fx letters: c = crush, t = tremolo, e = echo; cut = only part of the file plays)
static void drumpad_status(void* self, char* out, size_t cap) {
    const AT_Drumpad* d = (const AT_Drumpad*)self;

    int looping = 0;
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) looping += drumpad_is_looping(d, i);

    if (d->pat_stage >= 0) { drumpad_pattern_edit_status(d, out, cap); return; }
    if (d->selected < 0) { snprintf(out, cap, "no pads loaded"); return; }

    // the "/" cut editor: e.g. "F cut start ms (file 2350): 250_  Enter Esc"
    if (d->edit_pad >= 0) {
        char key[16];
        key_name(drumpad_pad_key(d->edit_pad), key, sizeof key);
        static const char* const field[] = { "start", "end", "wait", "play for" };
        const AT_DrumPad* e = &d->pads[d->edit_pad];
        if (d->edit_note[0])
            snprintf(out, cap, "%s %s ms: %s_  (%s)", key, field[d->edit_field], d->edit_text, d->edit_note);
        else if (d->edit_field == 0)
            snprintf(out, cap, "%s 1/4 start ms (file %.0f): %s_  Enter Esc", key, drumpad_length_ms(d, d->edit_pad), d->edit_text);
        else if (d->edit_field == 1)
            snprintf(out, cap, "%s 2/4 end ms (%.0f.., now %.0f): %s_", key, d->edit_from_ms,
                     e->cut_in_ms ? e->cut_to_ms : e->cut_to * drumpad_length_ms(d, d->edit_pad), d->edit_text);
        else if (d->edit_field == 2)
            snprintf(out, cap, "%s 3/4 wait ms before it plays (now %.0f): %s_", key, e->wait_ms, d->edit_text);
        else
            snprintf(out, cap, "%s 4/4 play for ms (0 = part length, now %.0f): %s_", key, e->play_ms, d->edit_text);
        return;
    }

    const AT_DrumPad* p = &d->pads[d->selected];
    char key[16];
    key_name(drumpad_pad_key(d->selected), key, sizeof key);

    char fx[8] = "";
    int  n     = 0;
    for (int i = 0; i < AT_PAD_FX_COUNT; i++)
        if (p->fx[i]) fx[n++] = AT_PAD_FX_NAMES[i][0];
    fx[n] = '\0';

    // the cut: "250-1000ms" (set in ms), "cut" (set as fractions), nothing (whole file)
    char cut[24] = "";
    if (p->cut_in_ms)                              snprintf(cut, sizeof cut, " %.0f-%.0fms", p->cut_from_ms, p->cut_to_ms);
    else if (p->cut_from > 0.0f || p->cut_to < 1.0f) snprintf(cut, sizeof cut, " cut");

    // timing: " wait2000 for1000"
    char timing[32] = "";
    if (p->wait_ms > 0.0f && p->play_ms > 0.0f) snprintf(timing, sizeof timing, " wait%.0f for%.0f", p->wait_ms, p->play_ms);
    else if (p->wait_ms > 0.0f)                 snprintf(timing, sizeof timing, " wait%.0f", p->wait_ms);
    else if (p->play_ms > 0.0f)                 snprintf(timing, sizeof timing, " for%.0f", p->play_ms);

    snprintf(out, cap, "%s %.12s %+.0fst%s%s %s v%.1f%s%s%s%s | %d loop%s", key, p->label, p->pitch,
             p->keep_tempo ? " tempo" : "", p->loop ? " loop" : "", p->sync ? "sync" : "free", p->volume,
             n ? " fx:" : "", fx, cut, timing, looping, looping == 1 ? "" : "s");

    // the pattern: " | pat 4x4 3/4"
    int step = drumpad_pattern_step(d);
    if (step >= 0) {
        size_t used = strlen(out);
        snprintf(out + used, cap > used ? cap - used : 0, " | pat %dx%d %d/%d",
                 d->pattern.rows, d->pattern.steps, step + 1, d->pattern.steps);
    }
}

static void pattern_tick(void* self) {
    drumpad_pattern_tick((AT_Drumpad*)self);
}

AT_Instrument drumpad_instrument(AT_Drumpad* d) {
    return (AT_Instrument){
        .name        = "drumpad",
        .help        = d->help,
        .self        = d,
        .attach      = drumpad_attach,
        .key         = pad_key,
        .release_all = pad_release_all,
        .status      = drumpad_status,
        .demo_start  = drumpad_demo_start,
        .tick        = pattern_tick
    };
}
