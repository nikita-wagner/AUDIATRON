// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine.c — settings, curves, files, the instrument
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/at_math.h"
#include "io/audio_file/audio_file.h"

const char* const AT_MACHINE_CURVE_NAMES[AT_MACHINE_CURVES] = {
    "gain", "from", "to", "rate",
    "crush_downsample", "crush_bits", "tremolo_rate", "tremolo_depth",
    "echo_time", "echo_feedback", "echo_mix",
    "eq_low", "eq_mid_freq", "eq_mid_gain", "eq_high", "eq_lowpass",
};
const char* const AT_MACHINE_FX_NAMES[AT_MACHINE_FX_COUNT] = { "eq", "crush", "tremolo", "echo" };
const char* const AT_MACHINE_SHAPE_NAMES[AT_MACHINE_SHAPES] = { "linear", "ease in", "ease out", "s-curve", "custom" };


// ─── Curves ──────────────────────────────────────────────────────────────────

float machine_shape_eval(const AT_MachineCurve* c, float t) {
    t = at_clampf(t, 0.0f, 1.0f);
    switch (c->shape) {
    case AT_MACHINE_SHAPE_EASE_IN:  return powf(t, c->amount);
    case AT_MACHINE_SHAPE_EASE_OUT: return 1.0f - powf(1.0f - t, c->amount);
    case AT_MACHINE_SHAPE_S_CURVE:  return t * t * (3.0f - 2.0f * t);
    case AT_MACHINE_SHAPE_CUSTOM: {
        const float y[5] = { 0.0f, c->points[0], c->points[1], c->points[2], 1.0f };
        int   seg = (int)(t * 4.0f);
        if (seg > 3) seg = 3;
        const float u = t * 4.0f - (float)seg;
        return y[seg] + (y[seg + 1] - y[seg]) * u;
    }
    case AT_MACHINE_SHAPE_LINEAR:
    default:
        return t;
    }
}

float machine_curve_eval(const AT_MachineCurve* c, float t) {
    return c->idle + (c->full - c->idle) * machine_shape_eval(c, t);
}

static AT_MachineCurve curve(float idle, float full, AT_MachineShape shape) {
    return (AT_MachineCurve){ idle, full, shape, 2.0f, { 0.25f, 0.5f, 0.75f } };
}

// ─── Sending settings: directly before attach, through the engine after ─────

static void send(AT_Machine* m, int param, float value) {
    if (m->engine) engine_set_param(m->engine, machine_set_param, &m->sound, param, value);
    else           machine_set_param(&m->sound, param, value);
}

// What the curves read: the throttle itself, or with the gearbox on the engine's rev (machine_gearbox.c).
void machine_send_drive(AT_Machine* m, float value, bool force) {
    value = at_clampf(value, 0.0f, 1.0f);
    if (!force && fabsf(value - m->sent) < 0.0015f) return;            // ~10 rpm: enough for the ear, and a recording stays small
    m->sent = value;
    send(m, AT_MACHINE_PARAM_THROTTLE, value);
}

void machine_set_throttle(AT_Machine* m, float throttle) {
    m->throttle = at_clampf(throttle, 0.0f, 1.0f);
    if (m->gearbox.mode == AT_MACHINE_GEARBOX_OFF) machine_send_drive(m, m->throttle, true);
}

void machine_set_running(AT_Machine* m, bool running) {
    m->running = running;
    if (m->engine) {
        // sent ahead with engine_at, it happens then: only after that can the sound say otherwise
        const uint64_t now = engine_now(m->engine);
        m->running_frame = m->engine->stamp_frame > now ? m->engine->stamp_frame : now;
    }
    send(m, AT_MACHINE_PARAM_RUNNING, running ? 1.0f : 0.0f);
}

void machine_set_volume(AT_Machine* m, float volume) {
    m->volume = at_clampf(volume, 0.0f, 4.0f);
    send(m, AT_MACHINE_PARAM_VOLUME, m->volume);
}

void machine_set_smooth_ms(AT_Machine* m, float ms) {
    m->smooth_ms = at_clampf(ms, 1.0f, 5000.0f);
    send(m, AT_MACHINE_PARAM_SMOOTH_MS, m->smooth_ms);
}

void machine_set_xfade_ms(AT_Machine* m, float ms) {
    m->xfade_ms = at_clampf(ms, 0.0f, 1000.0f);
    send(m, AT_MACHINE_PARAM_XFADE_MS, m->xfade_ms);
}

void machine_set_curve(AT_Machine* m, AT_MachineCurveId id, const AT_MachineCurve* c) {
    if ((int)id < 0 || id >= AT_MACHINE_CURVES) return;
    AT_MachineCurve v = *c;
    v.amount = at_clampf(v.amount, 1.0f, 6.0f);
    if ((int)v.shape < 0 || v.shape >= AT_MACHINE_SHAPES) v.shape = AT_MACHINE_SHAPE_LINEAR;
    for (int i = 0; i < AT_MACHINE_POINTS; i++) v.points[i] = at_clampf(v.points[i], 0.0f, 1.0f);
    m->curves[id] = v;

    send(m, AT_MACHINE_PARAM_CURVE(id, AT_MACHINE_FIELD_IDLE),   v.idle);
    send(m, AT_MACHINE_PARAM_CURVE(id, AT_MACHINE_FIELD_FULL),   v.full);
    send(m, AT_MACHINE_PARAM_CURVE(id, AT_MACHINE_FIELD_SHAPE),  (float)v.shape);
    send(m, AT_MACHINE_PARAM_CURVE(id, AT_MACHINE_FIELD_AMOUNT), v.amount);
    for (int i = 0; i < AT_MACHINE_POINTS; i++)
        send(m, AT_MACHINE_PARAM_CURVE(id, AT_MACHINE_FIELD_POINT1 + i), v.points[i]);
}

// The defaults asked for: idle loops 2.0..4.0 s at a low gain, full throttle loops 2.0..3.0 s at full gain.
void machine_reset_curves(AT_Machine* m) {
    const AT_MachineCurve defaults[AT_MACHINE_CURVES] = {
        [AT_MACHINE_CURVE_GAIN] = curve(0.35f, 1.0f, AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_FROM] = curve(2.0f,  2.0f, AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_TO]   = curve(4.0f,  3.0f, AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_RATE] = curve(1.0f,  1.0f, AT_MACHINE_SHAPE_LINEAR),
        // the effects' settings (they only sound while their effect is on)
        [AT_MACHINE_CURVE_CRUSH_DOWNSAMPLE] = curve(1.0f,    6.0f,     AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_CRUSH_BITS]       = curve(12.0f,   6.0f,     AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_TREMOLO_RATE]     = curve(6.0f,    22.0f,    AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_TREMOLO_DEPTH]    = curve(0.5f,    0.15f,    AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_ECHO_TIME]        = curve(140.0f,  70.0f,    AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_ECHO_FEEDBACK]    = curve(0.35f,   0.25f,    AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_ECHO_MIX]         = curve(0.25f,   0.15f,    AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_EQ_LOW]           = curve(3.0f,    1.0f,     AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_EQ_MID_FREQ]      = curve(300.0f,  1500.0f,  AT_MACHINE_SHAPE_EASE_IN),
        [AT_MACHINE_CURVE_EQ_MID_GAIN]      = curve(0.0f,    4.0f,     AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_EQ_HIGH]          = curve(-4.0f,   2.0f,     AT_MACHINE_SHAPE_LINEAR),
        [AT_MACHINE_CURVE_EQ_LOWPASS]       = curve(1800.0f, 14000.0f, AT_MACHINE_SHAPE_EASE_IN),   // the muffler opens
    };
    for (int i = 0; i < AT_MACHINE_CURVES; i++) machine_set_curve(m, (AT_MachineCurveId)i, &defaults[i]);
}

// ─── The machine's own effects ───────────────────────────────────────────────

// The echo is ~1 MB: made the first time it is switched on, set to the curves' idle values.
static bool reserve_echo(AT_Machine* m) {
    if (m->echo_memory) return true;
    AT_Echo* e = (AT_Echo*)calloc(1, sizeof(AT_Echo));
    if (!e) return false;
    echo_init(e, m->sample_rate, m->curves[AT_MACHINE_CURVE_ECHO_TIME].idle,
              m->curves[AT_MACHINE_CURVE_ECHO_FEEDBACK].idle, m->curves[AT_MACHINE_CURVE_ECHO_MIX].idle);
    m->echo_memory = e;
    m->sound.echo  = e;                 // read by the audio thread only once echo is switched on (sent after this)
    return true;
}

bool machine_set_fx(AT_Machine* m, AT_MachineFx fx, bool on) {
    if ((int)fx < 0 || fx >= AT_MACHINE_FX_COUNT) return false;
    if (on && fx == AT_MACHINE_FX_ECHO) {
        if (!reserve_echo(m)) return false;
        AT_Machine* copy = (AT_Machine*)m->copy;            // the replay may switch the copy's echo on
        if (copy) reserve_echo(copy);
    }
    m->fx[fx] = on;
    send(m, AT_MACHINE_PARAM_FX_EQ + (int)fx, on ? 1.0f : 0.0f);
    return true;
}

// ─── Files ───────────────────────────────────────────────────────────────────

const AT_Clip* machine_clip(const AT_Machine* m) {
    return m->sound.slots ? &m->sound.slots[m->slot] : NULL;
}

double machine_seconds(const AT_Machine* m) {
    const AT_Clip* c = machine_clip(m);
    return c ? audio_clip_seconds(c) : 0.0;
}

// Puts `fresh` (owned from now on) into play: directly before attach, else into
// the slot that is not playing, then tells the sound (and the replay's copy) to switch.
static bool use_clip(AT_Machine* m, AT_Clip* fresh, char* error, size_t error_cap) {
    if (!m->owns_clips) {
        audio_clip_set_error(error, error_cap, "the replay's copy plays the original's file");
        audio_clip_free(fresh);
        return false;
    }
    if (!m->engine) {
        audio_clip_free(&m->clips[m->slot]);
        m->clips[m->slot] = *fresh;
        return true;
    }
    const int other = 1 - m->slot;
    // The other slot played until the last switch; that switch must have reached the audio thread.
    if (m->clips[other].data && engine_now(m->engine) < m->slot_frame + 2 * AT_MAX_BLOCK_FRAMES) {
        audio_clip_set_error(error, error_cap, "the last file is still being switched in, try again");
        audio_clip_free(fresh);
        return false;
    }
    audio_clip_free(&m->clips[other]);
    m->clips[other] = *fresh;
    m->slot         = other;
    m->slot_frame   = engine_now(m->engine);
    engine_set_param(m->engine, machine_set_param, &m->sound, AT_MACHINE_PARAM_CLIP, (float)other);

    AT_Machine* copy = (AT_Machine*)m->copy;
    if (copy && copy->engine) {
        copy->slot = other;
        engine_set_param(copy->engine, machine_set_param, &copy->sound, AT_MACHINE_PARAM_CLIP, (float)other);
    }
    return true;
}

bool machine_load(AT_Machine* m, const char* path, char* error, size_t error_cap) {
    AT_Clip fresh = { 0 };
    if (!audio_file_load(path, &fresh, error, error_cap)) return false;
    if (!use_clip(m, &fresh, error, error_cap)) return false;

    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(m->label, sizeof m->label, "%s", name);
    snprintf(m->path, sizeof m->path, "%s", path);
    return true;
}

bool machine_load_builtin(AT_Machine* m, AT_MachineBuiltin kind) {
    if ((int)kind < 0 || kind >= AT_MACHINE_BUILTINS) return false;
    AT_Clip fresh = { 0 };
    if (!machine_builtin_render(kind, &fresh)) return false;
    if (!use_clip(m, &fresh, NULL, 0)) return false;
    snprintf(m->label, sizeof m->label, "built-in %s", AT_MACHINE_BUILTIN_LABELS[kind]);
    m->path[0] = '\0';
    m->builtin = kind;
    return true;
}

static void sound_init(AT_MachineSound* s, float sample_rate, const AT_Clip* slots, int slot) {
    memset(s, 0, sizeof *s);
    bitcrusher_init(&s->crush, 1, 16.0f);
    s->slots       = slots;
    s->slot        = slot;
    s->sample_rate = sample_rate;
    s->volume      = 1.0f;
    s->smooth_ms   = 120.0f;
    s->xfade_ms    = 40.0f;
}

bool machine_init(AT_Machine* m, float sample_rate) {
    memset(m, 0, sizeof *m);
    m->owns_clips  = true;
    m->sample_rate = sample_rate;
    m->volume      = 1.0f;
    m->smooth_ms   = 120.0f;
    m->xfade_ms    = 40.0f;
    m->rise_per_s  = 0.6f;              // W held: idle to full in 1.7 s
    m->fall_per_s  = 0.9f;              // S held: full to idle in 1.1 s
    m->idle_per_s  = 0.4f;
    sound_init(&m->sound, sample_rate, m->clips, 0);
    machine_reset_curves(m);
    machine_gearbox_defaults(&m->gearbox);              // a six-speed car, switched off
    m->drive.rpm = m->drive.free_rpm = m->gearbox.idle_rpm;
    m->drive.clutch = 1.0f;
    return machine_load_builtin(m, AT_MACHINE_BUILTIN_PETROL);
}

void machine_init_copy(AT_Machine* m, AT_Machine* original, float sample_rate) {
    memset(m, 0, sizeof *m);
    m->owns_clips  = false;
    m->sample_rate = sample_rate;
    m->slot        = original->slot;
    m->volume      = original->volume;
    m->smooth_ms   = original->smooth_ms;
    m->xfade_ms    = original->xfade_ms;
    m->rise_per_s  = original->rise_per_s;
    m->fall_per_s  = original->fall_per_s;
    m->idle_per_s  = original->idle_per_s;
    snprintf(m->label, sizeof m->label, "%s", original->label);
    snprintf(m->path, sizeof m->path, "%s", original->path);
    sound_init(&m->sound, sample_rate, original->clips, original->slot);      // the same file, not owned
    m->sound.volume    = m->volume;
    m->sound.smooth_ms = m->smooth_ms;
    m->sound.xfade_ms  = m->xfade_ms;
    for (int i = 0; i < AT_MACHINE_CURVES; i++) {
        m->curves[i]       = original->curves[i];
        m->sound.curves[i] = original->curves[i];
    }
    if (original->echo_memory) reserve_echo(m);             // so the replay can switch the copy's echo on
    original->copy = m;
}

void machine_free(AT_Machine* m) {
    if (m->owns_clips) {
        audio_clip_free(&m->clips[0]);
        audio_clip_free(&m->clips[1]);
    }
    memset(m->clips, 0, sizeof m->clips);
    m->sound.slots = NULL;
    free(m->echo_memory);               // the audio thread has stopped
    m->echo_memory = NULL;
    m->sound.echo  = NULL;
    m->copy        = NULL;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void machine_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_Machine* m = (AT_Machine*)self;
    m->engine    = engine;
    m->channel   = channel;
    m->last_tick = 0;
    m->sound.engine  = engine;
    m->sound.channel = channel;
    engine_set_source(engine, channel, &AT_MACHINE_SOURCE, &m->sound);
}

static bool machine_key_adapter(void* self, const AT_KeyEvent* ev) {
    return machine_key((AT_Machine*)self, ev);
}

static void machine_release_adapter(void* self) {
    machine_release_all((AT_Machine*)self);
}

// e.g. "on thr 45% 2.00-3.55s built-in petrol engine"
static void machine_status(void* self, char* out, size_t cap) {
    const AT_Machine* m = (const AT_Machine*)self;
    char name[18];
    snprintf(name, sizeof name, "%.16s", m->label);
    char gear[64] = "";
    if (m->gearbox.mode != AT_MACHINE_GEARBOX_OFF) {
        char g[8];
        if (m->drive.gear == 0)      snprintf(g, sizeof g, "N");
        else if (m->drive.gear < 0)  snprintf(g, sizeof g, "R");
        else                    snprintf(g, sizeof g, "%d", m->drive.gear);
        snprintf(gear, sizeof gear, " %s %s %.0frpm %.0fkm/h%s", m->gearbox.mode == AT_MACHINE_GEARBOX_AUTOMATIC ? "auto" : "manual",
                 g, m->drive.rpm, m->drive.speed, m->drive.phase ? " shifting" : "");
    }
    snprintf(out, cap, "%s thr %.0f%% %.2f-%.2fs%s%s %s", m->running ? "on" : "off", m->throttle * 100.0f,
             machine_curve_eval(&m->curves[AT_MACHINE_CURVE_FROM], m->sent),
             machine_curve_eval(&m->curves[AT_MACHINE_CURVE_TO], m->sent),
             gear, m->auto_idle ? " auto-idle" : "", name);
}

// --offline: the machine runs at half throttle instead of the arpeggio.
static void machine_demo(void* self) {
    AT_Machine* m = (AT_Machine*)self;
    machine_set_running(m, true);
    machine_set_throttle(m, 0.5f);
}

static void machine_tick_adapter(void* self) {
    machine_tick((AT_Machine*)self);
}

AT_Instrument machine_instrument(AT_Machine* m) {
    return (AT_Instrument){
        .name        = "machine",
        .help        = MACHINE_HELP,
        .self        = m,
        .attach      = machine_attach,
        .key         = machine_key_adapter,
        .release_all = machine_release_adapter,
        .status      = machine_status,
        .demo_start  = machine_demo,
        .tick        = machine_tick_adapter
    };
}
