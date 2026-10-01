#include "synth/synth.h"
#include "synth/synth_voices.h"

// ─── Init / parameters ───────────────────────────────────────────────────────

void synth_init(AT_Synth* s, float sample_rate) {
    memset(s, 0, sizeof *s);
    s->sample_rate = sample_rate;
    s->wave        = AT_WAVE_SAW;
    s->adsr        = (AT_ADSRParams){ 0.005f, 0.20f, 0.6f, 0.35f };
    s->gain        = 0.2f;
    for (int i = 0; i < AT_SYNTH_VOICES; i++) {
        oscillator_init(&s->voices[i].osc, 0x9E3779B9u ^ (uint32_t)(i * 7919 + 1));
        adsr_reset(&s->voices[i].env);
    }
}

void synth_set_param(void* self, int param, float value) {
    AT_Synth* s = (AT_Synth*)self;

    switch ((AT_SynthParam)param) {
    case AT_SYNTH_PARAM_WAVEFORM:
        if (value >= 0.0f && value < (float)AT_WAVE_COUNT) s->wave = (AT_Waveform)(int)value;
        break;
    case AT_SYNTH_PARAM_ATTACK:  s->adsr.attack_s  = value; break;
    case AT_SYNTH_PARAM_DECAY:   s->adsr.decay_s   = value; break;
    case AT_SYNTH_PARAM_SUSTAIN: s->adsr.sustain   = value; break;
    case AT_SYNTH_PARAM_RELEASE: s->adsr.release_s = value; break;
    case AT_SYNTH_PARAM_GAIN:    s->gain           = value; break;
    case AT_SYNTH_PARAM_DETUNE:  s->detune_cents   = value; break;
    }
}

// ─── Notes ───────────────────────────────────────────────────────────────────

void synth_note_on(AT_Synth* s, int note, float velocity) {
    AT_SynthVoice* v = synth_pick_voice(s, note);
    if (!v->active) {               // fresh voice: start silent at phase 0
        v->osc.phase = 0.0;
        v->env.level = 0.0f;
    }                               // retrigger/steal: keep phase + level (no click)
    v->active   = true;
    v->note     = note;
    v->velocity = at_clampf(velocity, 0.0f, 1.0f);
    v->age      = ++s->age_counter;
    oscillator_set_frequency(&v->osc, (double)(at_note_to_freq(note) * at_cents_to_ratio(s->detune_cents)),
                             (double)s->sample_rate);
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

void synth_render_mono(AT_Synth* s, float* out, uint32_t frames) {
    memset(out, 0, frames * sizeof(float));

    const AT_ADSRRates rates = adsr_rates(&s->adsr, s->sample_rate);

    for (int n = 0; n < AT_SYNTH_VOICES; n++) {
        AT_SynthVoice* v = &s->voices[n];
        if (!v->active) continue;

        const float amp = v->velocity * s->gain;
        for (uint32_t i = 0; i < frames; i++) {
            float env = adsr_tick(&v->env, &rates);
            if (adsr_is_idle(&v->env)) { v->active = false; break; }

            out[i] += oscillator_tick(&v->osc, s->wave) * env * amp;
        }
    }
}
