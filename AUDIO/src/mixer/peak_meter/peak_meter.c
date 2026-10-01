#include "mixer/peak_meter/peak_meter.h"

#include "core/at_math.h"

void peak_meter_init(AT_PeakMeter* m) {
    for (int c = 0; c < AT_MAX_CHANNELS; c++) m->peak[c] = 0.0f;
}

void peak_meter_measure(AT_PeakMeter* m, const AT_Buffer* buf) {
    for (uint32_t c = 0; c < buf->channels && c < AT_MAX_CHANNELS; c++) {
        const float* x    = buf->ch[c];
        float        peak = 0.0f;
        for (uint32_t i = 0; i < buf->frames; i++)
            peak = fmaxf(peak, fabsf(x[i]));
        m->peak[c] = peak;
    }
}

float peak_meter_read(const AT_PeakMeter* m, uint32_t channel) {
    return channel < AT_MAX_CHANNELS ? m->peak[channel] : 0.0f;
}
