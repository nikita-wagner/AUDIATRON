// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine_builtin.c — the built-in machine sounds
//
//  So a machine plays without any file. Each sound is 6 s, synthesized when it
//  is chosen, and EVEN (the same character all the way through), so any part
//  of it loops cleanly; how it changes with the throttle comes from its preset:
//
//    petrol      four-cylinder car engine at a lumpy idle: exhaust pulses (a noise
//                burst and a thump per firing), a muffler, valve-train ticking.
//                Preset: revs up to 2.4x, the muffler opens, automatic gearbox
//    jet         turbofan: broadband roar, low rumble, fan whine at 3.15 kHz with
//                its harmonics, a little hiss. Preset: spools slowly (900 ms follow),
//                whine and roar rise, the low-pass opens to 16 kHz
//    propeller   piston aircraft, 3-blade propeller at 2300 rpm: blade-pass pressure
//                pulses (115 Hz, blades a little unequal), the air chopped by the
//                blades, a nine-cylinder radial's exhaust. Preset: rate 0.6 -> 1.3
//    rocket      rocket motor: heavy low roar, mid roar, hiss, and the crackle of
//                sparse sharp impulses, all slowly fluttering. Preset: echo off the pad
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "core/at_math.h"
#include "dsp/biquad/biquad.h"

#define RATE     44100
#define SECONDS  6
#define FRAMES   (RATE * SECONDS)
#define PI_F     3.14159265f

const char* const AT_MACHINE_BUILTIN_NAMES[AT_MACHINE_BUILTINS]  = { "petrol", "jet", "propeller", "rocket" };
const char* const AT_MACHINE_BUILTIN_LABELS[AT_MACHINE_BUILTINS] = { "petrol engine", "jet engine", "propeller engine", "rocket engine" };

bool machine_find_builtin(const char* name, AT_MachineBuiltin* out) {
    for (int i = 0; name && i < AT_MACHINE_BUILTINS; i++)
        if (!_stricmp(name, AT_MACHINE_BUILTIN_NAMES[i])) { *out = (AT_MachineBuiltin)i; return true; }
    return false;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

static float noise(uint32_t* state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return (float)(*state & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

static void noise_fill(float* x, uint32_t* seed) {
    for (uint32_t i = 0; i < FRAMES; i++) x[i] = noise(seed);
}

static void filter(float* x, AT_FilterType type, float freq, float q, float gain_db) {
    const AT_BiquadCoeffs c = biquad_design(type, (float)RATE, freq, q, gain_db);
    AT_BiquadState s = { 0 };
    biquad_process(&c, &s, x, FRAMES);
}

// Scales x to this RMS, so the parts of a sound mix in known proportions.
static void set_rms(float* x, float rms) {
    double sum = 0.0;
    for (uint32_t i = 0; i < FRAMES; i++) sum += (double)x[i] * x[i];
    const float now = (float)sqrt(sum / FRAMES);
    if (now <= 0.0f) return;
    const float k = rms / now;
    for (uint32_t i = 0; i < FRAMES; i++) x[i] *= k;
}

// Out of memory: frees what was allocated.
static bool fail(float* mix, float* parts[], int count) {
    for (int p = 0; p < count; p++) free(parts[p]);
    free(mix);
    return false;
}

// Normalizes the mix to 0.8 of full scale into a mono clip; frees the parts.
static bool finish(float* mix, float* parts[], int count, AT_Clip* clip) {
    float peak = 1e-6f;
    for (uint32_t i = 0; i < FRAMES; i++) peak = fabsf(mix[i]) > peak ? fabsf(mix[i]) : peak;
    const bool ok = audio_clip_alloc(clip, FRAMES, 1, RATE);
    if (ok) {
        const float scale = 0.8f / peak * 32767.0f;
        for (uint32_t i = 0; i < FRAMES; i++) clip->data[i] = (int16_t)lrintf(mix[i] * scale);
    }
    for (int p = 0; p < count; p++) free(parts[p]);
    free(mix);
    return ok;
}

// ─── Petrol engine ───────────────────────────────────────────────────────────

static bool make_petrol(AT_Clip* clip) {
    float* x = (float*)malloc(FRAMES * sizeof(float));
    if (!x) return false;

    static const float cylinder[4] = { 1.0f, 0.82f, 0.94f, 0.76f };
    uint32_t seed = 0x9E3779B9u;
    double   phase = 0.0, crank = 0.0;
    double   since = 1.0;                       // seconds since the last firing
    float    amp = 0.0f, low1 = 0.0f, low2 = 0.0f, prev_n = 0.0f;
    int      cyl = 0;

    for (uint32_t i = 0; i < FRAMES; i++) {
        const double t   = (double)i / RATE;
        const double rpm = 1150.0 * (1.0 + 0.025 * sin(2.0 * PI_F * 0.31 * t) + 0.012 * sin(2.0 * PI_F * 1.7 * t));
        phase += rpm / 60.0 * 2.0 / RATE;       // two firings per crank turn
        crank += rpm / 60.0 / RATE;
        if (phase >= 1.0) {
            phase -= 1.0;
            since = 0.0;
            cyl   = (cyl + 1) & 3;
            amp   = cylinder[cyl] * (0.85f + 0.3f * (noise(&seed) * 0.5f + 0.5f));
        }
        const float n     = noise(&seed);
        const float burst = n * expf((float)(-since / 0.006)) * 0.6f;
        const float thump = sinf((float)(2.0 * PI_F * 68.0 * since)) * expf((float)(-since / 0.014)) * 0.9f;
        since += 1.0 / RATE;

        low1 += (amp * (burst + thump) - low1) * 0.12f;           // muffler
        low2 += (low1 - low2) * 0.22f;
        const float tick  = (n - prev_n) * 0.012f * (0.5f + 0.5f * (float)sin(2.0 * PI_F * crank * 4.0));   // valve train
        prev_n = n;
        const float whine = 0.015f * sinf((float)(2.0 * PI_F * crank * 8.0));
        x[i] = low2 * 1.7f + tick + whine;
    }
    return finish(x, NULL, 0, clip);
}

// ─── Jet engine (turbofan) ───────────────────────────────────────────────────

static bool make_jet(AT_Clip* clip) {
    float* mix    = (float*)calloc(FRAMES, sizeof(float));
    float* roar   = (float*)malloc(FRAMES * sizeof(float));
    float* rumble = (float*)malloc(FRAMES * sizeof(float));
    float* hiss   = (float*)malloc(FRAMES * sizeof(float));
    float* parts[] = { roar, rumble, hiss };
    if (!mix || !roar || !rumble || !hiss) return fail(mix, parts, 3);

    uint32_t seed = 0x2545F491u;
    noise_fill(roar, &seed);                    // the jet's broadband roar, centred low-mid
    filter(roar, AT_FILTER_BANDPASS, 700.0f, 0.45f, 0.0f);
    filter(roar, AT_FILTER_LOWPASS, 4500.0f, 0.707f, 0.0f);
    set_rms(roar, 0.25f);
    noise_fill(rumble, &seed);                  // the low rumble you feel
    filter(rumble, AT_FILTER_LOWPASS, 80.0f, 0.9f, 0.0f);
    filter(rumble, AT_FILTER_LOWPASS, 80.0f, 0.9f, 0.0f);
    set_rms(rumble, 0.2f);
    noise_fill(hiss, &seed);
    filter(hiss, AT_FILTER_HIGHPASS, 5000.0f, 0.707f, 0.0f);
    set_rms(hiss, 0.03f);

    // the fan's blade-pass whine and its harmonics, wavering a little in pitch and level
    double p1 = 0, p2 = 0, p3 = 0;
    for (uint32_t i = 0; i < FRAMES; i++) {
        const double t = (double)i / RATE;
        const double wobble = 1.0 + 0.003 * sin(2.0 * PI_F * 0.7 * t) + 0.0015 * sin(2.0 * PI_F * 2.3 * t);
        const float  flutter = 1.0f + 0.2f * (float)sin(2.0 * PI_F * 3.3 * t) * (float)sin(2.0 * PI_F * 0.45 * t);
        p1 += 3150.0 * wobble / RATE;
        p2 += 6300.0 * wobble / RATE;
        p3 += 1575.0 * wobble / RATE;
        const float whine = 0.08f * flutter * (float)sin(2.0 * PI_F * p1) + 0.035f * (float)sin(2.0 * PI_F * p2) + 0.03f * (float)sin(2.0 * PI_F * p3);
        mix[i] = roar[i] + rumble[i] + hiss[i] + whine;
    }
    return finish(mix, parts, 3, clip);
}

// ─── Propeller engine ────────────────────────────────────────────────────────

static bool make_propeller(AT_Clip* clip) {
    float* mix    = (float*)calloc(FRAMES, sizeof(float));
    float* chop   = (float*)malloc(FRAMES * sizeof(float));
    float* engine = (float*)calloc(FRAMES, sizeof(float));
    float* rumble = (float*)malloc(FRAMES * sizeof(float));
    float* blades = (float*)calloc(FRAMES, sizeof(float));
    float* parts[] = { chop, engine, rumble, blades };
    if (!mix || !chop || !engine || !rumble || !blades) return fail(mix, parts, 4);

    const double rpm = 2300.0;
    const double blade_hz = rpm / 60.0 * 3.0;       // 3 blades: 115 Hz
    const double fire_hz  = rpm / 60.0 * 4.5;       // nine-cylinder radial, four-stroke: 172.5 Hz
    static const float blade_amp[3] = { 1.0f, 0.92f, 0.97f };
    static const float cylinder[9]  = { 1.0f, 0.86f, 0.95f, 0.8f, 0.92f, 0.88f, 0.97f, 0.83f, 0.9f };

    uint32_t seed = 0x1B873593u;
    noise_fill(chop, &seed);
    filter(chop, AT_FILTER_LOWPASS, 2500.0f, 0.707f, 0.0f);
    noise_fill(rumble, &seed);
    filter(rumble, AT_FILTER_LOWPASS, 60.0f, 0.9f, 0.0f);
    set_rms(rumble, 0.1f);

    double bp = 0, fp = 0, since_blade = 1, since_fire = 1;
    int    blade = 0, cyl = 0;
    float  fire_amp = 1.0f;
    for (uint32_t i = 0; i < FRAMES; i++) {
        const double t = (double)i / RATE;
        const double wobble = 1.0 + 0.004 * sin(2.0 * PI_F * 0.4 * t);
        bp += blade_hz * wobble / RATE;
        fp += fire_hz * wobble / RATE;
        if (bp >= 1.0) { bp -= 1.0; since_blade = 0; blade = (blade + 1) % 3; }
        if (fp >= 1.0) { fp -= 1.0; since_fire = 0; cyl = (cyl + 1) % 9; fire_amp = cylinder[cyl] * (0.85f + 0.3f * (noise(&seed) * 0.5f + 0.5f)); }
        // a blade passing: a sharp pressure pulse with a slower negative tail
        const float pulse = expf((float)(-since_blade / 0.0016)) - 0.35f * expf((float)(-since_blade / 0.0048));
        blades[i] = blade_amp[blade] * pulse;
        // the air, chopped by the blades
        chop[i] *= 0.35f + 0.65f * expf((float)(-since_blade / 0.004));
        // exhaust pops
        engine[i] = fire_amp * noise(&seed) * expf((float)(-since_fire / 0.0025));
        since_blade += 1.0 / RATE;
        since_fire  += 1.0 / RATE;
    }
    filter(engine, AT_FILTER_LOWPASS, 1500.0f, 0.707f, 0.0f);
    set_rms(blades, 0.2f);
    set_rms(chop, 0.12f);
    set_rms(engine, 0.12f);
    for (uint32_t i = 0; i < FRAMES; i++) mix[i] = blades[i] + chop[i] + engine[i] + rumble[i];
    return finish(mix, parts, 4, clip);
}

// ─── Rocket engine ───────────────────────────────────────────────────────────

static bool make_rocket(AT_Clip* clip) {
    float* mix     = (float*)calloc(FRAMES, sizeof(float));
    float* roar    = (float*)malloc(FRAMES * sizeof(float));
    float* mid     = (float*)malloc(FRAMES * sizeof(float));
    float* hiss    = (float*)malloc(FRAMES * sizeof(float));
    float* crackle = (float*)calloc(FRAMES, sizeof(float));
    float* flutter = (float*)malloc(FRAMES * sizeof(float));
    float* parts[] = { roar, mid, hiss, crackle, flutter };
    if (!mix || !roar || !mid || !hiss || !crackle || !flutter) return fail(mix, parts, 5);

    uint32_t seed = 0x68E31DA4u;
    noise_fill(roar, &seed);                    // the heavy low roar
    filter(roar, AT_FILTER_LOWPASS, 300.0f, 0.8f, 0.0f);
    filter(roar, AT_FILTER_LOWPASS, 300.0f, 0.8f, 0.0f);
    set_rms(roar, 0.3f);
    noise_fill(mid, &seed);
    filter(mid, AT_FILTER_BANDPASS, 1000.0f, 0.6f, 0.0f);
    set_rms(mid, 0.12f);
    noise_fill(hiss, &seed);
    filter(hiss, AT_FILTER_HIGHPASS, 3500.0f, 0.707f, 0.0f);
    set_rms(hiss, 0.05f);

    // crackle: sparse, sharp impulses of very different strength (about 250 a second)
    for (uint32_t i = 0; i < FRAMES - 32; i++) {
        if (noise(&seed) * 0.5f + 0.5f > 250.0f / RATE) continue;
        float a = noise(&seed) * 0.5f + 0.5f;
        a = a * a * a * a * (noise(&seed) < 0.0f ? -1.0f : 1.0f);
        for (int k = 0; k < 18; k++) crackle[i + k] += a * noise(&seed) * expf(-(float)k / 4.0f);
    }
    filter(crackle, AT_FILTER_HIGHPASS, 1500.0f, 0.707f, 0.0f);
    set_rms(crackle, 0.06f);

    // the whole roar breathes: a slow random flutter (noise around 3 Hz)
    noise_fill(flutter, &seed);
    filter(flutter, AT_FILTER_LOWPASS, 3.0f, 0.707f, 0.0f);
    filter(flutter, AT_FILTER_LOWPASS, 3.0f, 0.707f, 0.0f);
    set_rms(flutter, 0.2f);
    for (uint32_t i = 0; i < FRAMES; i++)
        mix[i] = (roar[i] + mid[i]) * (1.0f + flutter[i]) + hiss[i] + crackle[i];
    return finish(mix, parts, 5, clip);
}

bool machine_builtin_render(AT_MachineBuiltin kind, AT_Clip* clip) {
    switch (kind) {
    case AT_MACHINE_BUILTIN_PETROL:    return make_petrol(clip);
    case AT_MACHINE_BUILTIN_JET:       return make_jet(clip);
    case AT_MACHINE_BUILTIN_PROPELLER: return make_propeller(clip);
    case AT_MACHINE_BUILTIN_ROCKET:    return make_rocket(clip);
    default:                           return false;
    }
}

// ─── Presets ─────────────────────────────────────────────────────────────────

static void set(AT_Machine* m, AT_MachineCurveId id, float idle, float full, AT_MachineShape shape, float amount) {
    AT_MachineCurve c = m->curves[id];
    c.idle   = idle;
    c.full   = full;
    c.shape  = shape;
    c.amount = amount;
    machine_set_curve(m, id, &c);
}

void machine_preset(AT_Machine* m, AT_MachineBuiltin kind) {
    machine_reset_curves(m);
    for (int fx = 0; fx < AT_MACHINE_FX_COUNT; fx++) machine_set_fx(m, (AT_MachineFx)fx, false);
    machine_set_gearbox_mode(m, AT_MACHINE_GEARBOX_OFF);
    m->auto_idle  = false;
    m->rise_per_s = 0.6f;
    m->fall_per_s = 0.9f;
    m->idle_per_s = 0.4f;

    switch (kind) {
    case AT_MACHINE_BUILTIN_PETROL:
        // a car: the curves read the engine's rev, the gearbox shifts by itself, the pedal lifts when W is let go
        set(m, AT_MACHINE_CURVE_GAIN, 0.35f, 1.0f, AT_MACHINE_SHAPE_EASE_OUT, 2.0f);
        set(m, AT_MACHINE_CURVE_RATE, 0.8f, 2.4f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        machine_set_fx(m, AT_MACHINE_FX_EQ, true);
        machine_set_smooth_ms(m, 40.0f);
        machine_gearbox_defaults(&m->gearbox);
        machine_set_gearbox_mode(m, AT_MACHINE_GEARBOX_AUTOMATIC);
        m->auto_idle  = true;
        m->rise_per_s = 1.5f;
        m->idle_per_s = 2.0f;
        break;
    case AT_MACHINE_BUILTIN_JET:
        set(m, AT_MACHINE_CURVE_GAIN, 0.25f, 1.0f, AT_MACHINE_SHAPE_EASE_IN, 1.6f);
        set(m, AT_MACHINE_CURVE_FROM, 1.0f, 1.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_TO, 5.0f, 5.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_RATE, 0.75f, 1.45f, AT_MACHINE_SHAPE_S_CURVE, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOWPASS, 2500.0f, 16000.0f, AT_MACHINE_SHAPE_EASE_IN, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOW, 4.0f, 2.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_FREQ, 3150.0f, 3150.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_GAIN, 0.0f, 4.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_HIGH, -6.0f, 3.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        machine_set_fx(m, AT_MACHINE_FX_EQ, true);
        machine_set_smooth_ms(m, 900.0f);       // a turbine spools up and down slowly
        break;
    case AT_MACHINE_BUILTIN_PROPELLER:
        set(m, AT_MACHINE_CURVE_GAIN, 0.35f, 1.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_FROM, 1.0f, 1.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_TO, 5.0f, 4.5f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_RATE, 0.6f, 1.3f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOWPASS, 1800.0f, 9000.0f, AT_MACHINE_SHAPE_EASE_IN, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOW, 5.0f, 3.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_FREQ, 300.0f, 800.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_GAIN, 0.0f, 2.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_HIGH, -6.0f, 0.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        machine_set_fx(m, AT_MACHINE_FX_EQ, true);
        machine_set_smooth_ms(m, 450.0f);
        break;
    case AT_MACHINE_BUILTIN_ROCKET:
        set(m, AT_MACHINE_CURVE_GAIN, 0.5f, 1.0f, AT_MACHINE_SHAPE_EASE_OUT, 2.0f);
        set(m, AT_MACHINE_CURVE_FROM, 0.5f, 0.5f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_TO, 5.5f, 5.5f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_RATE, 0.85f, 1.1f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOWPASS, 3500.0f, 18000.0f, AT_MACHINE_SHAPE_EASE_IN, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_LOW, 6.0f, 3.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_FREQ, 900.0f, 1200.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_MID_GAIN, 0.0f, 3.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_EQ_HIGH, -3.0f, 2.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_ECHO_TIME, 320.0f, 280.0f, AT_MACHINE_SHAPE_LINEAR, 2.0f);   // off the launch pad
        set(m, AT_MACHINE_CURVE_ECHO_FEEDBACK, 0.25f, 0.2f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        set(m, AT_MACHINE_CURVE_ECHO_MIX, 0.15f, 0.12f, AT_MACHINE_SHAPE_LINEAR, 2.0f);
        machine_set_fx(m, AT_MACHINE_FX_EQ, true);
        machine_set_fx(m, AT_MACHINE_FX_ECHO, true);
        machine_set_smooth_ms(m, 250.0f);
        break;
    default:
        break;
    }
}
