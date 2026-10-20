#include "synth/synth.h"
#include "synth/synth_voices.h"

// ─── Init / parameters ───────────────────────────────────────────────────────

void synth_init(AT_Synth* s, float sample_rate) {
    memset(s, 0, sizeof *s);
    s->sample_rate  = sample_rate;
    s->wave         = AT_WAVE_SAW;
    s->adsr         = (AT_ADSRParams){ 0.005f, 0.20f, 0.6f, 0.35f };
    s->gain         = 0.2f;
    s->filter_decay = 0.3f;
    s->bend_time    = 0.05f;
    s->pluck_bright = 0.5f;
    s->pluck_decay  = 3.0f;
    for (int i = 0; i < AT_SYNTH_VOICES; i++) {
        oscillator_init(&s->voices[i].osc, 0x9E3779B9u ^ (uint32_t)(i * 7919 + 1));
        adsr_reset(&s->voices[i].env);
        s->voices[i].rng = 0x2545F491u ^ (uint32_t)(i * 2654435761u + 7);
    }
}

void synth_set_param(void* self, int param, float value) {
    AT_Synth* s = (AT_Synth*)self;

    switch ((AT_SynthParam)param) {
    case AT_SYNTH_PARAM_WAVEFORM:
        if (value >= 0.0f && value < (float)AT_WAVE_COUNT) s->wave = (AT_Waveform)(int)value;
        break;
    case AT_SYNTH_PARAM_ATTACK:        s->adsr.attack_s  = value;                              break;
    case AT_SYNTH_PARAM_DECAY:         s->adsr.decay_s   = value;                              break;
    case AT_SYNTH_PARAM_SUSTAIN:       s->adsr.sustain   = value;                              break;
    case AT_SYNTH_PARAM_RELEASE:       s->adsr.release_s = value;                              break;
    case AT_SYNTH_PARAM_GAIN:          s->gain           = value;                              break;
    case AT_SYNTH_PARAM_DETUNE:        s->detune_cents   = value;                              break;
    case AT_SYNTH_PARAM_FILTER:        s->filter_hz      = value > 0.0f ? value : 0.0f;        break;
    case AT_SYNTH_PARAM_RESONANCE:     s->filter_res     = at_clampf(value, 0.0f, 1.0f);       break;
    case AT_SYNTH_PARAM_FILTER_ENV:    s->filter_env     = at_clampf(value, -8.0f, 8.0f);      break;
    case AT_SYNTH_PARAM_FILTER_DECAY:  s->filter_decay   = at_clampf(value, 0.0f, 20.0f);      break;
    case AT_SYNTH_PARAM_VIBRATO_RATE:  s->vibrato_hz     = at_clampf(value, 0.0f, 20.0f);      break;
    case AT_SYNTH_PARAM_VIBRATO_DEPTH: s->vibrato_cents  = at_clampf(value, 0.0f, 200.0f);     break;
    case AT_SYNTH_PARAM_VIBRATO_DELAY: s->vibrato_delay  = at_clampf(value, 0.0f, 10.0f);      break;
    case AT_SYNTH_PARAM_BEND:          s->bend_cents     = at_clampf(value, -2400.0f, 2400.0f); break;
    case AT_SYNTH_PARAM_BEND_TIME:     s->bend_time      = at_clampf(value, 0.0f, 5.0f);       break;
    case AT_SYNTH_PARAM_NOISE:         s->noise          = at_clampf(value, 0.0f, 1.0f);       break;
    case AT_SYNTH_PARAM_PLUCK_BRIGHT:  s->pluck_bright   = at_clampf(value, 0.0f, 1.0f);       break;
    case AT_SYNTH_PARAM_PLUCK_DECAY:   s->pluck_decay    = at_clampf(value, 0.05f, 30.0f);     break;
    }
}

// ─── A plucked string (Karplus–Strong) ───────────────────────────────────────
//
// The string is a delay line one period long. The pick fills it with a burst of
// noise (darker for a soft pick); every round the wave is averaged with its
// neighbour, which takes the highs out first, like a real string. A first-order
// all-pass makes up the fraction of a sample the period has, so it is in tune.

static inline float voice_noise(AT_SynthVoice* v) {
    uint32_t x = v->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    v->rng = x;
    return (float)((int32_t)x) * (1.0f / 2147483648.0f);
}

static void pluck_start(AT_Synth* s, AT_SynthVoice* v) {
    double period = (double)s->sample_rate / v->hz;
    while (period > AT_SYNTH_PLUCK_MAX - 4) period *= 0.5;      // lower than the line holds: an octave up
    if (period < 3.0) period = 3.0;
    const double d = period - 0.5;                              // the averaging delays by half a sample
    int    n    = (int)floor(d);
    double frac = d - n;
    if (frac < 0.1) { n--; frac += 1.0; }                       // the all-pass is best between 0.1 and 1.1
    v->pluck_len  = n;
    v->pluck_pos  = 0;
    v->pluck_prev = 0.0f;
    v->pluck_ap   = v->pluck_ap_in = 0.0f;
    v->pluck_c    = (float)((1.0 - frac) / (1.0 + frac));
    // the loss per round: down 60 dB after pluck_decay seconds
    v->pluck_gain = powf(0.001f, 1.0f / ((float)v->hz * s->pluck_decay));
    if (v->pluck_gain > 0.99995f) v->pluck_gain = 0.99995f;

    // the pick: noise through a one-pole low-pass, brighter when harder; no DC, peak 1
    const float bright = at_clampf(s->pluck_bright * (0.55f + 0.45f * v->velocity), 0.02f, 1.0f);
    const float b      = 0.04f + 0.96f * bright * bright;
    float y = 0.0f, mean = 0.0f, peak = 1e-6f;
    for (int i = 0; i < n; i++) {
        y += b * (voice_noise(v) - y);
        v->pluck[i] = y;
        mean += y;
    }
    mean /= (float)n;
    for (int i = 0; i < n; i++) {
        v->pluck[i] -= mean;
        if (fabsf(v->pluck[i]) > peak) peak = fabsf(v->pluck[i]);
    }
    const float scale = 0.9f / peak;
    for (int i = 0; i < n; i++) v->pluck[i] *= scale;
}

static inline float pluck_tick(AT_SynthVoice* v) {
    const float out = v->pluck[v->pluck_pos];
    const float avg = v->pluck_gain * 0.5f * (out + v->pluck_prev);
    v->pluck_prev = out;
    float ap = v->pluck_c * avg + v->pluck_ap_in - v->pluck_c * v->pluck_ap;
    if (fabsf(ap) < 1e-20f) ap = 0.0f;                          // no denormals once it has died away
    v->pluck_ap_in = avg;
    v->pluck_ap    = ap;
    v->pluck[v->pluck_pos] = ap;
    if (++v->pluck_pos >= v->pluck_len) v->pluck_pos = 0;
    return out;
}

// ─── Notes ───────────────────────────────────────────────────────────────────

void synth_note_on(AT_Synth* s, int note, float velocity) {
    AT_SynthVoice* v = synth_pick_voice(s, note);
    if (!v->active) {               // fresh voice: start silent at phase 0
        v->osc.phase = 0.0;
        v->env.level = 0.0f;
        v->lp1 = v->lp2 = 0.0f;
    }                               // retrigger/steal: keep phase + level (no click)
    v->active   = true;
    v->note     = note;
    v->velocity = at_clampf(velocity, 0.0f, 1.0f);
    v->age      = ++s->age_counter;
    v->frames   = 0;
    v->fenv     = 1.0f;
    v->hz       = (double)(at_note_to_freq(note) * at_cents_to_ratio(s->detune_cents));
    oscillator_set_frequency(&v->osc, v->hz, (double)s->sample_rate);
    if (s->wave == AT_WAVE_PLUCK) pluck_start(s, v);
    adsr_gate_on(&v->env);
}

void synth_note_off(AT_Synth* s, int note) {
    for (int i = 0; i < AT_SYNTH_VOICES; i++) {
        AT_SynthVoice* v = &s->voices[i];
        if (v->active && v->note == note)
            adsr_gate_off(&v->env, &s->adsr, s->sample_rate);
    }
}

void synth_all_notes_off(AT_Synth* s) {
    for (int i = 0; i < AT_SYNTH_VOICES; i++)
        if (s->voices[i].active)
            adsr_gate_off(&s->voices[i].env, &s->adsr, s->sample_rate);
}

// ─── Render ──────────────────────────────────────────────────────────────────

#define CHUNK 16                    // pitch and filter move every 16 samples

void synth_render_mono(AT_Synth* s, float* out, uint32_t frames) {
    memset(out, 0, frames * sizeof(float));

    const AT_ADSRRates rates  = adsr_rates(&s->adsr, s->sample_rate);
    const float        sr     = s->sample_rate;
    const bool         pluck  = s->wave == AT_WAVE_PLUCK;
    const bool         filter = s->filter_hz > 0.0f;
    const float        fenv_k = s->filter_decay > 0.0f ? expf(-1.0f / (s->filter_decay * sr)) : 0.0f;
    const bool         vib    = s->vibrato_cents > 0.0f && s->vibrato_hz > 0.0f;
    const bool         bend   = s->bend_cents != 0.0f && s->bend_time > 0.0f;
    const float        k      = 2.0f - 1.9f * s->filter_res;   // the filter's damping: 2 = none, 0.1 = ringing

    for (int n = 0; n < AT_SYNTH_VOICES; n++) {
        AT_SynthVoice* v = &s->voices[n];
        if (!v->active) continue;

        const float amp = v->velocity * s->gain;
        for (uint32_t i = 0; i < frames;) {
            const uint32_t chunk = frames - i < CHUNK ? frames - i : CHUNK;
            const double   t     = (double)v->frames / sr;

            // the pitch: vibrato fading in after its delay, a scoop gliding onto the note
            if (!pluck && (vib || bend)) {
                double cents = 0.0;
                if (vib && t > s->vibrato_delay) {
                    const double fade = (t - s->vibrato_delay) / 0.25;
                    cents += s->vibrato_cents * (fade < 1.0 ? fade : 1.0) * sin(AT_TWO_PI * s->vibrato_hz * t);
                }
                if (bend) cents += s->bend_cents * exp(-t / s->bend_time);
                oscillator_set_frequency(&v->osc, v->hz * pow(2.0, cents / 1200.0), (double)sr);
            }
            // the filter: its cutoff follows the note (half as fast) and opens by the envelope
            float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
            if (filter) {
                float fc = s->filter_hz * exp2f((float)(v->note - 60) / 24.0f + s->filter_env * v->fenv);
                fc = at_clampf(fc, 20.0f, 0.45f * sr);
                const float g = tanf((float)AT_PI * fc / sr);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            for (uint32_t j = 0; j < chunk; j++, i++) {
                const float env = adsr_tick(&v->env, &rates);
                if (adsr_is_idle(&v->env)) { v->active = false; break; }

                float x = pluck ? pluck_tick(v) : oscillator_tick(&v->osc, s->wave);
                if (s->noise > 0.0f) x += s->noise * voice_noise(v);       // breath, bow
                if (filter) {                                               // state-variable low-pass (TPT)
                    const float v3 = x - v->lp2;
                    const float v1 = a1 * v->lp1 + a2 * v3;
                    const float v2 = v->lp2 + a2 * v->lp1 + a3 * v3;
                    v->lp1 = 2.0f * v1 - v->lp1;
                    v->lp2 = 2.0f * v2 - v->lp2;
                    x = v2;
                }
                out[i] += x * env * amp;
                v->fenv *= fenv_k;
                v->frames++;
            }
            if (!v->active) break;
        }
    }
}
