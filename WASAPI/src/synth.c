#include "synth.h"

#define MIN_ENV_SECONDS 0.001f      // 1 ms floor keeps every segment click-free

const char* synth_waveform_name(AT_Waveform w) {
    switch (w) {
    case AT_WAVE_SINE:     return "sine";
    case AT_WAVE_SAW:      return "saw";
    case AT_WAVE_SQUARE:   return "square";
    case AT_WAVE_TRIANGLE: return "triangle";
    case AT_WAVE_NOISE:    return "noise";
    default:               return "?";
    }
}

void synth_init(AT_Synth* s, float sample_rate) {
    memset(s, 0, sizeof *s);
    s->sample_rate = sample_rate;
    s->wave        = AT_WAVE_SAW;
    s->adsr        = (AT_ADSRParams){ 0.005f, 0.20f, 0.6f, 0.35f };
    s->gain        = 0.2f;
    for (int i = 0; i < AT_MAX_VOICES; i++)
        s->voices[i].noise_state = 0x9E3779B9u ^ (uint32_t)(i * 7919 + 1);
}

// ─── Voice allocation ────────────────────────────────────────────────────────

static AT_Voice* pick_voice(AT_Synth* s, int note) {
    AT_Voice* free_voice = NULL;
    AT_Voice* steal      = NULL;

    for (int i = 0; i < AT_MAX_VOICES; i++) {
        AT_Voice* v = &s->voices[i];
        if (v->active && v->note == note) return v;
        if (!v->active) { if (!free_voice) free_voice = v; continue; }

        if (!steal) { steal = v; continue; }
        bool v_rel = v->stage == AT_ENV_RELEASE;
        bool s_rel = steal->stage == AT_ENV_RELEASE;
        if (v_rel != s_rel) { if (v_rel) steal = v; }
        else if (v->age < steal->age) steal = v;
    }
    return free_voice ? free_voice : steal;
}

void synth_note_on(AT_Synth* s, int note, float velocity) {
    AT_Voice* v = pick_voice(s, note);
    if (!v->active) {               // fresh voice: start silent at phase 0
        v->phase = 0.0;
        v->level = 0.0f;
    }                               // retrigger/steal: keep phase + level (no click)
    v->active    = true;
    v->note      = note;
    v->velocity  = at_clampf(velocity, 0.0f, 1.0f);
    v->phase_inc = (double)at_note_to_freq(note) / (double)s->sample_rate;
    v->stage     = AT_ENV_ATTACK;
    v->age       = ++s->age_counter;
}

void synth_note_off(AT_Synth* s, int note) {
    float release = fmaxf(s->adsr.release_s, MIN_ENV_SECONDS) * s->sample_rate;
    for (int i = 0; i < AT_MAX_VOICES; i++) {
        AT_Voice* v = &s->voices[i];
        if (!v->active || v->note != note || v->stage == AT_ENV_RELEASE) continue;
        v->stage        = AT_ENV_RELEASE;
        v->release_step = v->level / release;
    }
}

void synth_all_notes_off(AT_Synth* s) {
    for (int i = 0; i < AT_MAX_VOICES; i++)
        if (s->voices[i].active) synth_note_off(s, s->voices[i].note);
}

// ─── Oscillators ─────────────────────────────────────────────────────────────
// PolyBLEP smooths the one-sample discontinuity of saw/square, which removes
// most of the aliasing a naive saw produces at high notes.

static inline float poly_blep(double t, double dt) {
    if (t < dt)       { t /= dt;               return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return (float)(t * t + t + t + 1.0); }
    return 0.0f;
}

static inline float osc_sample(AT_Waveform wave, AT_Voice* v) {
    double t  = v->phase;
    double dt = v->phase_inc;

    switch (wave) {
    case AT_WAVE_SINE:
        return sinf((float)(AT_TWO_PI * t));
    case AT_WAVE_SAW:
        return (float)(2.0 * t - 1.0) - poly_blep(t, dt);
    case AT_WAVE_SQUARE: {
        double t2 = t + 0.5;
        if (t2 >= 1.0) t2 -= 1.0;
        return (t < 0.5 ? 1.0f : -1.0f) + poly_blep(t, dt) - poly_blep(t2, dt);
    }
    case AT_WAVE_TRIANGLE:
        return (float)(1.0 - 4.0 * fabs(t - 0.5));
    case AT_WAVE_NOISE: {
        uint32_t x = v->noise_state;            // xorshift32
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        v->noise_state = x;
        return (float)x * (2.0f / 4294967296.0f) - 1.0f;
    }
    default:
        return 0.0f;
    }
}

// ─── Envelope ────────────────────────────────────────────────────────────────

static inline float env_tick(AT_Voice* v, float attack_step, float decay_step, float sustain) {
    switch (v->stage) {
    case AT_ENV_ATTACK:
        v->level += attack_step;
        if (v->level >= 1.0f) { v->level = 1.0f; v->stage = AT_ENV_DECAY; }
        break;
    case AT_ENV_DECAY:
        v->level -= decay_step;
        if (v->level <= sustain) { v->level = sustain; v->stage = AT_ENV_SUSTAIN; }
        break;
    case AT_ENV_SUSTAIN:
        v->level = sustain;
        break;
    case AT_ENV_RELEASE:
        v->level -= v->release_step;
        if (v->level <= 0.0f) { v->level = 0.0f; v->stage = AT_ENV_IDLE; }
        break;
    case AT_ENV_IDLE:
        v->level = 0.0f;
        break;
    }
    return v->level;
}

// ─── Render ──────────────────────────────────────────────────────────────────

void synth_render(AT_Synth* s, float* out, uint32_t frames) {
    memset(out, 0, frames * sizeof(float));

    const float sr          = s->sample_rate;
    const float sustain     = at_clampf(s->adsr.sustain, 0.0f, 1.0f);
    const float attack_step = 1.0f / (fmaxf(s->adsr.attack_s, MIN_ENV_SECONDS) * sr);
    const float decay_step  = (1.0f - sustain) / (fmaxf(s->adsr.decay_s, MIN_ENV_SECONDS) * sr);

    for (int n = 0; n < AT_MAX_VOICES; n++) {
        AT_Voice* v = &s->voices[n];
        if (!v->active) continue;

        const float amp = v->velocity * s->gain;
        for (uint32_t i = 0; i < frames; i++) {
            float env = env_tick(v, attack_step, decay_step, sustain);
            if (v->stage == AT_ENV_IDLE) { v->active = false; break; }

            out[i] += osc_sample(s->wave, v) * env * amp;

            v->phase += v->phase_inc;
            if (v->phase >= 1.0) v->phase -= 1.0;
        }
    }
}
