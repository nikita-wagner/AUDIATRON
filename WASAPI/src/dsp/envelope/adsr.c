#include "dsp/envelope/adsr.h"

#include "core/at_math.h"

#define MIN_ENV_SECONDS 0.001f      // 1 ms floor keeps every segment click-free

AT_ADSRRates adsr_rates(const AT_ADSRParams* p, float sample_rate) {
    AT_ADSRRates r;
    r.sustain     = at_clampf(p->sustain, 0.0f, 1.0f);
    r.attack_step = 1.0f / (fmaxf(p->attack_s, MIN_ENV_SECONDS) * sample_rate);
    r.decay_step  = (1.0f - r.sustain) / (fmaxf(p->decay_s, MIN_ENV_SECONDS) * sample_rate);
    return r;
}

void adsr_reset(AT_ADSR* e) {
    e->stage        = AT_ENV_IDLE;
    e->level        = 0.0f;
    e->release_step = 0.0f;
}

void adsr_gate_on(AT_ADSR* e) {
    e->stage = AT_ENV_ATTACK;
}

void adsr_gate_off(AT_ADSR* e, const AT_ADSRParams* p, float sample_rate) {
    if (e->stage == AT_ENV_IDLE || e->stage == AT_ENV_RELEASE) return;
    float release_frames = fmaxf(p->release_s, MIN_ENV_SECONDS) * sample_rate;
    e->stage        = AT_ENV_RELEASE;
    e->release_step = e->level / release_frames;
}
