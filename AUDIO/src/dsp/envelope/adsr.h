// ═══════════════════════════════════════════════════════════════════════════
//  dsp/envelope/adsr.h — linear attack / decay / sustain / release envelope
//
//  A building block: one AT_ADSR is the envelope state of ONE voice. The
//  parameters (AT_ADSRParams) are shared by all voices of an instrument.
//
//  Usage per block:
//      AT_ADSRRates r = adsr_rates(&params, sample_rate);   // once per block
//      for each frame:  float level = adsr_tick(&env, &r);  // 0..1
//
//  gate_on never resets the level, so retriggering a sounding voice ramps up
//  from where it is instead of clicking down to zero first.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdbool.h>

typedef enum {
    AT_ENV_IDLE,
    AT_ENV_ATTACK,
    AT_ENV_DECAY,
    AT_ENV_SUSTAIN,
    AT_ENV_RELEASE
} AT_EnvStage;

typedef struct {
    float attack_s;
    float decay_s;
    float sustain;      // level 0..1
    float release_s;
} AT_ADSRParams;

// Per-sample steps derived from the params — compute once per block.
typedef struct {
    float attack_step;
    float decay_step;
    float sustain;
} AT_ADSRRates;

typedef struct {
    AT_EnvStage stage;
    float       level;          // current envelope level 0..1
    float       release_step;   // fixed at gate_off so the release always takes release_s
} AT_ADSR;

AT_ADSRRates adsr_rates(const AT_ADSRParams* p, float sample_rate);

void adsr_reset   (AT_ADSR* e);                                             // silent + idle
void adsr_gate_on (AT_ADSR* e);                                             // → attack, from the current level
void adsr_gate_off(AT_ADSR* e, const AT_ADSRParams* p, float sample_rate);  // → release

static inline bool adsr_is_idle(const AT_ADSR* e) { return e->stage == AT_ENV_IDLE; }

static inline float adsr_tick(AT_ADSR* e, const AT_ADSRRates* r) {
    switch (e->stage) {
    case AT_ENV_ATTACK:
        e->level += r->attack_step;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = AT_ENV_DECAY; }
        break;
    case AT_ENV_DECAY:
        e->level -= r->decay_step;
        if (e->level <= r->sustain) { e->level = r->sustain; e->stage = AT_ENV_SUSTAIN; }
        break;
    case AT_ENV_SUSTAIN:
        e->level = r->sustain;
        break;
    case AT_ENV_RELEASE:
        e->level -= e->release_step;
        if (e->level <= 0.0f) { e->level = 0.0f; e->stage = AT_ENV_IDLE; }
        break;
    case AT_ENV_IDLE:
        e->level = 0.0f;
        break;
    }
    return e->level;
}
