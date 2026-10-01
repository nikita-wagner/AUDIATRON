#include "effects/_template/template_effect.h"

#include "core/at_math.h"

// Control thread, before the effect is plugged in: set parameters, clear state.
void template_effect_init(AT_TemplateEffect* fx, float amount) {
    memset(fx, 0, sizeof *fx);
    fx->amount = at_clampf(amount, 0.0f, 2.0f);
}

// Audio thread, between two blocks (sent with engine_set_param).
void template_effect_set_param(void* self, int param, float value) {
    AT_TemplateEffect* fx = (AT_TemplateEffect*)self;

    switch ((AT_TemplateEffectParam)param) {
    case AT_TEMPLATE_EFFECT_PARAM_AMOUNT: fx->amount = at_clampf(value, 0.0f, 2.0f); break;
    }
}

// Audio thread, once per block. Change the buffer in place.
void template_effect_process(AT_Buffer* buf, void* user) {
    AT_TemplateEffect* fx = (AT_TemplateEffect*)user;

    for (uint32_t c = 0; c < buf->channels; c++) {
        float* x = buf->ch[c];
        for (uint32_t i = 0; i < buf->frames; i++) {
            x[i] *= fx->amount;                 // ← your DSP goes here
        }
        if (buf->frames > 0) fx->last_sample[c] = x[buf->frames - 1];
    }
}
