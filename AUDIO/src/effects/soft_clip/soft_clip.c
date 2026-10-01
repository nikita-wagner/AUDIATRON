#include "effects/soft_clip/soft_clip.h"

#include "core/at_math.h"

void soft_clip_init(AT_SoftClip* sc, float knee) {
    sc->knee = at_clampf(knee, 0.0f, 0.99f);
}

void soft_clip_set_param(void* self, int param, float value) {
    AT_SoftClip* sc = (AT_SoftClip*)self;

    switch ((AT_SoftClipParam)param) {
    case AT_SOFT_CLIP_PARAM_KNEE: sc->knee = at_clampf(value, 0.0f, 0.99f); break;
    }
}

void soft_clip_process(AT_Buffer* buf, void* user) {
    const AT_SoftClip* sc   = (const AT_SoftClip*)user;
    const float        knee = sc->knee;
    const float        room = 1.0f - knee;      // headroom the curve has to fit into

    for (uint32_t c = 0; c < buf->channels; c++) {
        float* x = buf->ch[c];
        for (uint32_t i = 0; i < buf->frames; i++) {
            float a = fabsf(x[i]);
            if (a <= knee) continue;
            float y = knee + room * tanhf((a - knee) / room);
            x[i] = x[i] < 0.0f ? -y : y;
        }
    }
}
