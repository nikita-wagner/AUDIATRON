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

#include <math.h>

#include "effects/bitcrusher/bitcrusher.h"
#include "effects/echo/echo.h"
#include "effects/pitch_shift/pitch_shift.h"
#include "effects/tremolo/tremolo.h"
#include "instruments/machine/machine.h"
#include "layers/layer_stack.h"
#include "sampler/sampler.h"
#include "synth/synth.h"

static const char* const k_synth[]   = { "wave", "attack", "decay", "sustain", "release", "gain", "detune",
                                         "filter", "resonance", "filter_env", "filter_decay", "vibrato_rate",
                                         "vibrato_depth", "vibrato_delay", "bend", "bend_time", "noise",
                                         "pluck_bright", "pluck_decay" };
static const char* const k_layers[]  = { "volume", "pan", "mute", "transpose" };
static const char* const k_sampler[] = { "play", "pause", "stop", "seek", "rate", "loop", "gain", "gate", "root", "from", "to", "from_ms", "to_ms", "delay_ms", "length_ms" };
static const char* const k_shifter[] = { "semitones", "ratio", "window" };
static const char* const k_crush[]   = { "downsample", "bits" };
static const char* const k_tremolo[] = { "rate", "depth" };
static const char* const k_echo[]    = { "time", "feedback", "mix", "damping", "ping_pong" };
// "track 6 machine throttle 0.45", "track 6 machine to_full 3". The ids have gaps (the curves start at 16): NULL there.
static const char* const k_machine[] = {
    "throttle", "running", "volume", "smooth_ms", "xfade_ms", "clip", "fx_eq", "fx_crush",
    "fx_tremolo", "fx_echo", NULL, NULL, NULL, NULL, NULL, NULL,
    "gain_idle", "gain_full", "gain_shape", "gain_amount", "gain_point1", "gain_point2", "gain_point3", NULL,
    "from_idle", "from_full", "from_shape", "from_amount", "from_point1", "from_point2", "from_point3", NULL,
    "to_idle", "to_full", "to_shape", "to_amount", "to_point1", "to_point2", "to_point3", NULL,
    "rate_idle", "rate_full", "rate_shape", "rate_amount", "rate_point1", "rate_point2", "rate_point3", NULL,
    "crush_downsample_idle", "crush_downsample_full", "crush_downsample_shape", "crush_downsample_amount", "crush_downsample_point1", "crush_downsample_point2", "crush_downsample_point3", NULL,
    "crush_bits_idle", "crush_bits_full", "crush_bits_shape", "crush_bits_amount", "crush_bits_point1", "crush_bits_point2", "crush_bits_point3", NULL,
    "tremolo_rate_idle", "tremolo_rate_full", "tremolo_rate_shape", "tremolo_rate_amount", "tremolo_rate_point1", "tremolo_rate_point2", "tremolo_rate_point3", NULL,
    "tremolo_depth_idle", "tremolo_depth_full", "tremolo_depth_shape", "tremolo_depth_amount", "tremolo_depth_point1", "tremolo_depth_point2", "tremolo_depth_point3", NULL,
    "echo_time_idle", "echo_time_full", "echo_time_shape", "echo_time_amount", "echo_time_point1", "echo_time_point2", "echo_time_point3", NULL,
    "echo_feedback_idle", "echo_feedback_full", "echo_feedback_shape", "echo_feedback_amount", "echo_feedback_point1", "echo_feedback_point2", "echo_feedback_point3", NULL,
    "echo_mix_idle", "echo_mix_full", "echo_mix_shape", "echo_mix_amount", "echo_mix_point1", "echo_mix_point2", "echo_mix_point3", NULL,
    "eq_low_idle", "eq_low_full", "eq_low_shape", "eq_low_amount", "eq_low_point1", "eq_low_point2", "eq_low_point3", NULL,
    "eq_mid_freq_idle", "eq_mid_freq_full", "eq_mid_freq_shape", "eq_mid_freq_amount", "eq_mid_freq_point1", "eq_mid_freq_point2", "eq_mid_freq_point3", NULL,
    "eq_mid_gain_idle", "eq_mid_gain_full", "eq_mid_gain_shape", "eq_mid_gain_amount", "eq_mid_gain_point1", "eq_mid_gain_point2", "eq_mid_gain_point3", NULL,
    "eq_high_idle", "eq_high_full", "eq_high_shape", "eq_high_amount", "eq_high_point1", "eq_high_point2", "eq_high_point3", NULL,
    "eq_lowpass_idle", "eq_lowpass_full", "eq_lowpass_shape", "eq_lowpass_amount", "eq_lowpass_point1", "eq_lowpass_point2", "eq_lowpass_point3", NULL
};

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
    TABLE(machine_set_param,     k_machine, false),
    TABLE(bitcrusher_set_param,  k_crush,   false),
    TABLE(tremolo_set_param,     k_tremolo, false),
    TABLE(echo_set_param,        k_echo,    false),
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

    if (!t || id < 0 || id >= t->count || !t->names[id])
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
        if (t->names[i] && !strcmp(name, t->names[i])) id = i;
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

// ─── Effects and a layer's synth: "rate 4.5" ─────────────────────────────────

static const struct {
    const char*  name;
    AT_ProcessFn process;                   // NULL: not an effect
    AT_ParamFn   param;
} k_objects[] = {
    { "crush",   bitcrusher_process, bitcrusher_set_param },
    { "tremolo", tremolo_process,    tremolo_set_param },
    { "echo",    echo_process,       echo_set_param },
    { "synth",   NULL,               synth_set_param },
};

const char* app_object_kind(AT_ParamFn fn) {
    for (size_t i = 0; i < sizeof k_objects / sizeof *k_objects; i++)
        if (k_objects[i].param == fn) return k_objects[i].name;
    return NULL;
}

AT_ParamFn app_effect_param_fn(AT_ProcessFn process) {
    for (size_t i = 0; process && i < sizeof k_objects / sizeof *k_objects; i++)
        if (k_objects[i].process == process) return k_objects[i].param;
    return NULL;
}

bool app_object_param_format(AT_ParamFn fn, int param, float value, char* out, size_t cap) {
    const ParamTable* t = app_object_kind(fn) ? table_for(fn) : NULL;
    if (!t || param < 0 || param >= t->count || !t->names[param]) return false;
    if (fn == echo_set_param && param == AT_ECHO_PARAM_TIME) return false;     // the track's "echo_ms" line has it
    snprintf(out, cap, "%s %.9g", t->names[param], value);
    return true;
}

bool app_object_param_parse(AT_ParamFn fn, const char* text, int* param, float* value) {
    const ParamTable* t = app_object_kind(fn) ? table_for(fn) : NULL;
    char  name[32] = "";
    float v        = 0.0f;
    if (!t || sscanf(text, " %31s %f", name, &v) != 2) return false;
    for (int i = 0; i < t->count; i++)
        if (t->names[i] && !strcmp(name, t->names[i])) { *param = i; *value = v; return true; }
    return false;
}

static void emit_object(AT_ParamFn fn, int param, float value, void (*emit)(void*, const char*), void* user) {
    char line[64];
    if (app_object_param_format(fn, param, value, line, sizeof line)) emit(user, line);
}

void app_object_snapshot(AT_ParamFn fn, const void* object, void (*emit)(void* user, const char* line), void* user) {
    if (!object) return;
    if (fn == bitcrusher_set_param) {
        const AT_Bitcrusher* b = (const AT_Bitcrusher*)object;
        emit_object(fn, AT_BITCRUSHER_PARAM_DOWNSAMPLE, (float)b->downsample, emit, user);
        emit_object(fn, AT_BITCRUSHER_PARAM_BITS, log2f(b->levels > 1.0f ? b->levels : 1.0f) + 1.0f, emit, user);    // levels = 2^(bits-1)
    }
    else if (fn == tremolo_set_param) {
        const AT_Tremolo* tr = (const AT_Tremolo*)object;
        emit_object(fn, AT_TREMOLO_PARAM_RATE,  tr->rate_hz, emit, user);
        emit_object(fn, AT_TREMOLO_PARAM_DEPTH, tr->depth,   emit, user);
    }
    else if (fn == echo_set_param) {
        const AT_Echo* e = (const AT_Echo*)object;
        emit_object(fn, AT_ECHO_PARAM_FEEDBACK,  e->feedback,                emit, user);
        emit_object(fn, AT_ECHO_PARAM_MIX,       e->mix,                     emit, user);
        emit_object(fn, AT_ECHO_PARAM_DAMPING,   e->damping,                 emit, user);
        emit_object(fn, AT_ECHO_PARAM_PING_PONG, e->ping_pong ? 1.0f : 0.0f, emit, user);
    }
    else if (fn == synth_set_param) {
        const AT_Synth* s = (const AT_Synth*)object;
        emit_object(fn, AT_SYNTH_PARAM_WAVEFORM, (float)s->wave,    emit, user);
        emit_object(fn, AT_SYNTH_PARAM_ATTACK,   s->adsr.attack_s,  emit, user);
        emit_object(fn, AT_SYNTH_PARAM_DECAY,    s->adsr.decay_s,   emit, user);
        emit_object(fn, AT_SYNTH_PARAM_SUSTAIN,  s->adsr.sustain,   emit, user);
        emit_object(fn, AT_SYNTH_PARAM_RELEASE,  s->adsr.release_s, emit, user);
        emit_object(fn, AT_SYNTH_PARAM_GAIN,     s->gain,           emit, user);
        emit_object(fn, AT_SYNTH_PARAM_DETUNE,   s->detune_cents,   emit, user);
        emit_object(fn, AT_SYNTH_PARAM_FILTER,        s->filter_hz,     emit, user);
        emit_object(fn, AT_SYNTH_PARAM_RESONANCE,     s->filter_res,    emit, user);
        emit_object(fn, AT_SYNTH_PARAM_FILTER_ENV,    s->filter_env,    emit, user);
        emit_object(fn, AT_SYNTH_PARAM_FILTER_DECAY,  s->filter_decay,  emit, user);
        emit_object(fn, AT_SYNTH_PARAM_VIBRATO_RATE,  s->vibrato_hz,    emit, user);
        emit_object(fn, AT_SYNTH_PARAM_VIBRATO_DEPTH, s->vibrato_cents, emit, user);
        emit_object(fn, AT_SYNTH_PARAM_VIBRATO_DELAY, s->vibrato_delay, emit, user);
        emit_object(fn, AT_SYNTH_PARAM_BEND,          s->bend_cents,    emit, user);
        emit_object(fn, AT_SYNTH_PARAM_BEND_TIME,     s->bend_time,     emit, user);
        emit_object(fn, AT_SYNTH_PARAM_NOISE,         s->noise,         emit, user);
        emit_object(fn, AT_SYNTH_PARAM_PLUCK_BRIGHT,  s->pluck_bright,  emit, user);
        emit_object(fn, AT_SYNTH_PARAM_PLUCK_DECAY,   s->pluck_decay,   emit, user);
    }
}

// Lines of a layer's synth or insert effect, put after "layers 2 tremolo".
typedef struct {
    void (*emit)(void*, const char*);
    void* user;
    char  prefix[48];
} PrefixedEmit;

static void prefixed_emit(void* user, const char* line) {
    PrefixedEmit* p = (PrefixedEmit*)user;
    char text[128];
    snprintf(text, sizeof text, "%s %s", p->prefix, line);
    p->emit(p->user, text);
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
        emit_param(t, AT_SYNTH_PARAM_FILTER,        s->filter_hz,     emit, user);
        emit_param(t, AT_SYNTH_PARAM_RESONANCE,     s->filter_res,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_FILTER_ENV,    s->filter_env,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_FILTER_DECAY,  s->filter_decay,  emit, user);
        emit_param(t, AT_SYNTH_PARAM_VIBRATO_RATE,  s->vibrato_hz,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_VIBRATO_DEPTH, s->vibrato_cents, emit, user);
        emit_param(t, AT_SYNTH_PARAM_VIBRATO_DELAY, s->vibrato_delay, emit, user);
        emit_param(t, AT_SYNTH_PARAM_BEND,          s->bend_cents,    emit, user);
        emit_param(t, AT_SYNTH_PARAM_BEND_TIME,     s->bend_time,     emit, user);
        emit_param(t, AT_SYNTH_PARAM_NOISE,         s->noise,         emit, user);
        emit_param(t, AT_SYNTH_PARAM_PLUCK_BRIGHT,  s->pluck_bright,  emit, user);
        emit_param(t, AT_SYNTH_PARAM_PLUCK_DECAY,   s->pluck_decay,   emit, user);
    }
    else if (t->fn == layer_stack_set_param) {
        const AT_LayerStack* st = (const AT_LayerStack*)t->live;
        for (uint32_t i = 0; i < st->count; i++) {
            const AT_Layer* l = &st->layers[i];
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_VOLUME),    l->strip.volume,          emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_PAN),       l->strip.pan,             emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_MUTE),      l->strip.mute ? 1.0f : 0.0f, emit, user);
            emit_param(t, AT_LAYER_PARAM(i, AT_LAYER_PARAM_TRANSPOSE), (float)l->transpose,      emit, user);

            // what is inside the layer: its synth, its insert effects ("layers 2 tremolo rate 5")
            PrefixedEmit p = { emit, user, "" };
            if (l->type == &AT_SYNTH_SOURCE) {
                snprintf(p.prefix, sizeof p.prefix, "%s %u synth", t->name, i + 1);
                app_object_snapshot(synth_set_param, l->self, prefixed_emit, &p);
            }
            for (int s = 0; s < AT_LAYER_INSERTS; s++) {
                AT_ParamFn fn = app_effect_param_fn(l->inserts[s].fn);
                if (!fn || !l->inserts[s].user) continue;
                snprintf(p.prefix, sizeof p.prefix, "%s %u %s", t->name, i + 1, app_object_kind(fn));
                app_object_snapshot(fn, l->inserts[s].user, prefixed_emit, &p);
            }
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
    else if (t->fn == machine_set_param) {
        const AT_MachineSound* s = (const AT_MachineSound*)t->live;
        emit_param(t, AT_MACHINE_PARAM_VOLUME,    s->volume,    emit, user);
        emit_param(t, AT_MACHINE_PARAM_SMOOTH_MS, s->smooth_ms, emit, user);
        emit_param(t, AT_MACHINE_PARAM_XFADE_MS,  s->xfade_ms,  emit, user);
        for (int c = 0; c < AT_MACHINE_CURVES; c++) {
            const AT_MachineCurve* k = &s->curves[c];
            emit_param(t, AT_MACHINE_PARAM_CURVE(c, AT_MACHINE_FIELD_IDLE),   k->idle,         emit, user);
            emit_param(t, AT_MACHINE_PARAM_CURVE(c, AT_MACHINE_FIELD_FULL),   k->full,         emit, user);
            emit_param(t, AT_MACHINE_PARAM_CURVE(c, AT_MACHINE_FIELD_SHAPE),  (float)k->shape, emit, user);
            emit_param(t, AT_MACHINE_PARAM_CURVE(c, AT_MACHINE_FIELD_AMOUNT), k->amount,       emit, user);
            for (int p = 0; p < AT_MACHINE_POINTS; p++)
                emit_param(t, AT_MACHINE_PARAM_CURVE(c, AT_MACHINE_FIELD_POINT1 + p), k->points[p], emit, user);
        }
        for (int fx = 0; fx < AT_MACHINE_FX_COUNT; fx++)
            emit_param(t, AT_MACHINE_PARAM_FX_EQ + fx, s->fx_on[fx] ? 1.0f : 0.0f, emit, user);
        emit_param(t, AT_MACHINE_PARAM_THROTTLE, s->throttle_target,       emit, user);
        emit_param(t, AT_MACHINE_PARAM_RUNNING,  s->running ? 1.0f : 0.0f, emit, user);
    }
    else if (t->fn == pitch_shift_set_param) {
        const AT_PitchShift* p = (const AT_PitchShift*)t->live;
        emit_param(t, AT_PITCH_SHIFT_PARAM_RATIO, p->ratio, emit, user);
    }
}
