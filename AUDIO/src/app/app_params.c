// ═══════════════════════════════════════════════════════════════════════════
//  app/app_params.c — instrument parameters as words, for recordings
//
//  The instruments change their sound only through engine_set_param on a few
//  modules: the synth, a layer stack, a sampler, a pitch shifter. A recording
//  writes such a change as words and a replay reads it back:
//
//      track 1 synth wave 2              synth_set_param(.., AT_SYNTH_PARAM_WAVEFORM, 2)
//      track 2 layers 2 mute 1           layer_stack_set_param(.., AT_LAYER_PARAM(1, MUTE), 1)
//      track 5 sampler rate 1.12246203   sampler_set_param(.., AT_SAMPLER_PARAM_RATE, ..)
//      track 5 sampler play              sampler_set_param(.., AT_SAMPLER_PARAM_PLAY, 0)
//      track 5 shifter ratio 1           pitch_shift_set_param(.., AT_PITCH_SHIFT_PARAM_RATIO, 1)
//
//  Values are written with 9 significant digits, so a float comes back as
//  exactly the same float. The tables below follow the modules' enums.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effects/pitch_shift/pitch_shift.h"
#include "layers/layer_stack.h"
#include "sampler/sampler.h"
#include "synth/synth.h"

static const char* const k_synth[]   = { "wave", "attack", "decay", "sustain", "release", "gain", "detune" };
static const char* const k_layers[]  = { "volume", "pan", "mute", "transpose" };
static const char* const k_sampler[] = { "play", "pause", "stop", "seek", "rate", "loop", "gain", "gate", "root", "from", "to", "from_ms", "to_ms", "delay_ms", "length_ms" };
static const char* const k_shifter[] = { "semitones", "ratio", "window" };

typedef struct {
    AT_ParamFn         fn;
    const char* const* names;
    int                count;
    bool               layered;             // param = AT_LAYER_PARAM(layer, id): "layers <n> <name>"
} ParamTable;

#define TABLE(fn, names, layered) { fn, names, (int)(sizeof names / sizeof *names), layered }

static const ParamTable k_tables[] = {
    TABLE(synth_set_param,       k_synth,   false),
    TABLE(layer_stack_set_param, k_layers,  true),
    TABLE(sampler_set_param,     k_sampler, false),
    TABLE(pitch_shift_set_param, k_shifter, false),
};

static const ParamTable* table_for(AT_ParamFn fn) {
    for (size_t i = 0; i < sizeof k_tables / sizeof *k_tables; i++)
        if (k_tables[i].fn == fn) return &k_tables[i];
    return NULL;
}

// Commands whose value means nothing.
static bool valueless(const ParamTable* t, int id) {
    return t->fn == sampler_set_param && (id == AT_SAMPLER_PARAM_PLAY || id == AT_SAMPLER_PARAM_STOP);
}

// ─── Words ───────────────────────────────────────────────────────────────────

void app_param_format(const AT_AppTarget* target, int param, float value, char* out, size_t cap) {
    const ParamTable* t = table_for(target->fn);
    int layer = t && t->layered ? param >> 8 : -1;
    int id    = t && t->layered ? param & 0xFF : param;

    char where[16] = "";
    if (layer >= 0) snprintf(where, sizeof where, " %d", layer + 1);

    if (!t || id < 0 || id >= t->count)
        snprintf(out, cap, "%s%s %d %.9g", target->name, where, id, value);         // unknown: by number
    else if (valueless(t, id))
        snprintf(out, cap, "%s%s %s", target->name, where, t->names[id]);
    else
        snprintf(out, cap, "%s%s %s %.9g", target->name, where, t->names[id], value);
}

bool app_param_parse(const AT_AppTarget* target, const char* text, int* param, float* value) {
    const ParamTable* t = table_for(target->fn);
    if (!t) return false;

    char name[32] = "";
    int  layer    = 0;
    int  used     = 0;
    if (t->layered) {
        if (sscanf(text, " %d %31s%n", &layer, name, &used) != 2 || layer < 1) return false;
        layer--;
    } else {
        if (sscanf(text, " %31s%n", name, &used) != 1) return false;
    }

    int id = -1;
    for (int i = 0; i < t->count; i++)
        if (!strcmp(name, t->names[i])) id = i;
    if (id < 0) {                                           // a number works too
        char* end;
        long  n = strtol(name, &end, 10);
        if (*end || n < 0) return false;
        id = (int)n;
    }

    *value = 0.0f;
    if (!valueless(t, id)) {
        char* end;
        double v = strtod(text + used, &end);
        if (end == text + used) return false;
        *value = (float)v;
    }
    *param = t->layered ? AT_LAYER_PARAM(layer, id) : id;
    return true;
}

// ─── Finding targets ─────────────────────────────────────────────────────────

const AT_AppTarget* app_target_find_live(const AT_App* app, AT_ParamFn fn, const void* self) {
    for (uint32_t i = 0; i < app->target_count; i++)
        if (app->targets[i].fn == fn && app->targets[i].live == self) return &app->targets[i];
    return NULL;
}

const AT_AppTarget* app_target_find_name(const AT_App* app, int track, const char* name) {
    for (uint32_t i = 0; i < app->target_count; i++)
        if (app->targets[i].track == track && !strcmp(app->targets[i].name, name)) return &app->targets[i];
    return NULL;
}

// ─── The state of a target, as lines (the start of a recording) ─────────────

static void emit_param(const AT_AppTarget* t, int param, float value,
                       void (*emit)(void*, const char*), void* user) {
    char line[96];
    app_param_format(t, param, value, line, sizeof line);
    emit(user, line);
}

void app_target_snapshot(const AT_AppTarget* t, void (*emit)(void* user, const char* line), void* user) {
    if (t->fn == synth_set_param) {
        const AT_Synth* s = (const AT_Synth*)t->live;
        emit_param(t, AT_SYNTH_PARAM_WAVEFORM, (float)s->wave,     emit, user);
        emit_param(t, AT_SYNTH_PARAM_ATTACK,   s->adsr.attack_s,   emit, user);
        emit_param(t, AT_SYNTH_PARAM_DECAY,    s->adsr.decay_s,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_SUSTAIN,  s->adsr.sustain,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_RELEASE,  s->adsr.release_s,  emit, user);
        emit_param(t, AT_SYNTH_PARAM_GAIN,     s->gain,            emit, user);
        emit_param(t, AT_SYNTH_PARAM_DETUNE,   s->detune_cents,    emit, user);
    }
    else if (t->fn == layer_stack_set_param) {
        const AT_LayerStack* st = (const AT_LayerStack*)t->live;
        for (uint32_t i = 0; i < st->count; i++) {
            const AT_Layer* l = &st->layers[i];
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_VOLUME),    l->strip.volume,          emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_PAN),       l->strip.pan,             emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_MUTE),      l->strip.mute ? 1.0f : 0.0f, emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_TRANSPOSE), (float)l->transpose,      emit, user);
        }
    }
    else if (t->fn == sampler_set_param) {
        const AT_Sampler* s = (const AT_Sampler*)t->live;
        emit_param(t, AT_SAMPLER_PARAM_RATE,        s->rate,                     emit, user);
        emit_param(t, AT_SAMPLER_PARAM_LOOP,        s->loop ? 1.0f : 0.0f,       emit, user);
        emit_param(t, AT_SAMPLER_PARAM_GAIN,        s->gain,                     emit, user);
        emit_param(t, AT_SAMPLER_PARAM_GATE,        s->gate ? 1.0f : 0.0f,       emit, user);
        emit_param(t, AT_SAMPLER_PARAM_ROOT,        (float)s->root_note,         emit, user);
        emit_param(t, AT_SAMPLER_PARAM_REGION_FROM, s->region_from,              emit, user);
        emit_param(t, AT_SAMPLER_PARAM_REGION_TO,   s->region_to,                emit, user);
        emit_param(t, AT_SAMPLER_PARAM_REGION_FROM_MS, s->region_from_ms,        emit, user);
        emit_param(t, AT_SAMPLER_PARAM_REGION_TO_MS,   s->region_to_ms,          emit, user);
        emit_param(t, AT_SAMPLER_PARAM_DELAY_MS,       s->delay_ms,              emit, user);
        emit_param(t, AT_SAMPLER_PARAM_LENGTH_MS,      s->length_ms,             emit, user);
        if (s->playing && s->clip && s->clip->sample_rate) {   // a song already playing goes on from there
            emit_param(t, AT_SAMPLER_PARAM_SEEK, (float)s->position_frames / (float)s->clip->sample_rate, emit, user);
            emit_param(t, AT_SAMPLER_PARAM_PLAY, 0.0f, emit, user);
        }
    }
    else if (t->fn == pitch_shift_set_param) {
        const AT_PitchShift* p = (const AT_PitchShift*)t->live;
        emit_param(t, AT_PITCH_SHIFT_PARAM_RATIO, p->ratio, emit, user);
    }
}
