#include "effects/tremolo/tremolo.h"

#include "core/at_math.h"

void tremolo_init(AT_Tremolo* t, float rate_hz, float depth) {
    t->rate_hz = rate_hz;
    t->depth   = at_clampf(depth, 0.0f, 1.0f);
    t->origin  = 0;
    t->restart = 0;
}

void tremolo_set_param(void* self, int param, float value) {
    AT_Tremolo* t = (AT_Tremolo*)self;

    switch ((AT_TremoloParam)param) {
    case AT_TREMOLO_PARAM_RATE:  t->rate_hz = value;                         break;
    case AT_TREMOLO_PARAM_DEPTH: t->depth   = at_clampf(value, 0.0f, 1.0f);  break;
    case AT_TREMOLO_PARAM_RESTART: t->restart = 1;                           break;
    }
}

void tremolo_process(AT_Buffer* buf, void* user) {
    AT_Tremolo* t = (AT_Tremolo*)user;
    double      w = AT_TWO_PI * t->rate_hz / buf->sample_rate;

    // a timed command splits the block where it lands: this block starts on its frame
    if (t->restart) { t->origin = buf->frame_clock; t->restart = 0; }
    const uint64_t from = buf->frame_clock >= t->origin ? buf->frame_clock - t->origin : 0;
    for (uint32_t i = 0; i < buf->frames; i++) {
        double phase = w * (double)(from + i);
        float  gain  = 1.0f - t->depth * 0.5f * (1.0f - (float)cos(phase));
        for (uint32_t c = 0; c < buf->channels; c++)
            buf->ch[c][i] *= gain;
    }
}
