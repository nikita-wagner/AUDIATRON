#include "engine/engine.h"

#define LIMITER_KNEE 0.8f       // master soft clip: linear below, curved above

void engine_init(AT_Engine* e, float sample_rate) {
    memset(e, 0, sizeof *e);
    e->sample_rate = sample_rate;
    at_queue_init(&e->queue);

    for (int c = 0; c < AT_ENGINE_CHANNELS; c++) {
        eq_init(&e->channel[c].eq, sample_rate);
        channel_strip_init(&e->channel[c].strip, sample_rate);
    }

    eq_init(&e->master_eq, sample_rate);
    master_fader_init(&e->master_fader, sample_rate);
    soft_clip_init(&e->limiter, LIMITER_KNEE);
    e->limiter_enabled = true;
    peak_meter_init(&e->master_meter);
}

void engine_shutdown(AT_Engine* e) {
    at_queue_destroy(&e->queue);
}
