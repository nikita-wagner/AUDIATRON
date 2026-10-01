#include "mixer/master_fader/master_fader.h"

#include "core/at_math.h"

#define SMOOTH_SECONDS 0.010f

void master_fader_init(AT_MasterFader* f, float sample_rate) {
    f->volume      = 0.8f;
    f->gain        = f->volume;     // start settled
    f->smooth_coef = at_smoothing_coef(SMOOTH_SECONDS, sample_rate);
}

void master_fader_process(AT_MasterFader* f, AT_Buffer* bus) {
    float g = f->gain, t = f->volume, k = f->smooth_coef;
    for (uint32_t i = 0; i < bus->frames; i++) {
        g += (t - g) * k;
        for (uint32_t c = 0; c < bus->channels; c++)
            bus->ch[c][i] *= g;
    }
    f->gain = g;
}
