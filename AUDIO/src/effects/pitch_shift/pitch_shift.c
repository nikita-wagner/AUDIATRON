#include "effects/pitch_shift/pitch_shift.h"

#include "core/at_math.h"

#define RATIO_MIN      0.25f
#define RATIO_MAX      4.0f
#define WINDOW_MIN_MS  10.0f
#define WINDOW_MAX_MS  100.0f
#define GLIDE_SECONDS  0.03f
#define IDLE_EPSILON   1e-4f

_Static_assert((AT_PITCH_SHIFT_CAPACITY & (AT_PITCH_SHIFT_CAPACITY - 1)) == 0,
               "AT_PITCH_SHIFT_CAPACITY must be a power of two");

void pitch_shift_init(AT_PitchShift* p, float sample_rate) {
    memset(p, 0, sizeof *p);
    p->sample_rate   = sample_rate;
    p->ratio         = 1.0f;
    p->current_ratio = 1.0f;
    p->window_ms     = 50.0f;
    p->glide_coef    = at_smoothing_coef(GLIDE_SECONDS, sample_rate);
}

void pitch_shift_set_param(void* self, int param, float value) {
    AT_PitchShift* p = (AT_PitchShift*)self;

    switch ((AT_PitchShiftParam)param) {
    case AT_PITCH_SHIFT_PARAM_SEMITONES:
        p->ratio = at_clampf(powf(2.0f, at_clampf(value, -24.0f, 24.0f) / 12.0f), RATIO_MIN, RATIO_MAX);
        break;
    case AT_PITCH_SHIFT_PARAM_RATIO:
        p->ratio = at_clampf(value, RATIO_MIN, RATIO_MAX);
        break;
    case AT_PITCH_SHIFT_PARAM_WINDOW_MS:
        p->window_ms = at_clampf(value, WINDOW_MIN_MS, WINDOW_MAX_MS);
        break;
    }
}

// Linear interpolation at `write - delay` (delay in frames, > 0).
static inline float read_line(const float* line, uint32_t write, float delay) {
    float   pos    = (float)write - delay;
    float   floor_ = floorf(pos);
    float   frac   = pos - floor_;
    int64_t index  = (int64_t)floor_;

    uint32_t a = (uint32_t)(index & (AT_PITCH_SHIFT_CAPACITY - 1));
    uint32_t b = (uint32_t)((index + 1) & (AT_PITCH_SHIFT_CAPACITY - 1));
    return line[a] + frac * (line[b] - line[a]);
}

void pitch_shift_process(AT_Buffer* buf, void* user) {
    AT_PitchShift* p = (AT_PitchShift*)user;

    const uint32_t channels = buf->channels > AT_MAX_CHANNELS ? AT_MAX_CHANNELS : buf->channels;
    float window = p->window_ms * 0.001f * p->sample_rate;
    float max_window = (float)(AT_PITCH_SHIFT_CAPACITY / 2 - 4);
    if (window > max_window) window = max_window;           // both taps must stay inside the ring
    if (window < 16.0f)      window = 16.0f;

    for (uint32_t i = 0; i < buf->frames; i++) {
        p->current_ratio += (p->ratio - p->current_ratio) * p->glide_coef;

        // The shifted sound lags the dry one by up to a window, so switching the
        // effect on or off is a crossfade (wet 0..1), never a jump.
        float wet_target = fabsf(p->ratio - 1.0f) < IDLE_EPSILON ? 0.0f : 1.0f;
        p->wet += (wet_target - p->wet) * p->glide_coef;

        // The ring is always written, so a later shift starts with real audio in it.
        float dry[AT_MAX_CHANNELS];
        for (uint32_t c = 0; c < channels; c++) {
            dry[c] = buf->ch[c][i];
            p->line[c][p->write] = dry[c];
        }
        if (p->wet < IDLE_EPSILON && wet_target == 0.0f) {      // settled at "unchanged"
            p->wet = 0.0f;
            p->write = (p->write + 1) & (AT_PITCH_SHIFT_CAPACITY - 1);
            continue;
        }

        // Two taps half a cycle apart; each one's delay sweeps 0..window and wraps.
        float phase_a = p->phase;
        float phase_b = phase_a + 0.5f;
        if (phase_b >= 1.0f) phase_b -= 1.0f;

        float s_a = sinf((float)AT_PI * phase_a);
        float s_b = sinf((float)AT_PI * phase_b);
        float gain_a = s_a * s_a;                           // sin²(πp) + sin²(π(p+½)) = 1
        float gain_b = s_b * s_b;

        float delay_a = 2.0f + phase_a * window;            // +2: never read the frame being written
        float delay_b = 2.0f + phase_b * window;

        for (uint32_t c = 0; c < channels; c++) {
            float shifted = gain_a * read_line(p->line[c], p->write, delay_a)
                          + gain_b * read_line(p->line[c], p->write, delay_b);
            buf->ch[c][i] = dry[c] + p->wet * (shifted - dry[c]);
        }

        // The delay moves by (1 - ratio) frames per frame: that is what makes the pitch change.
        p->phase += (1.0f - p->current_ratio) / window;
        p->phase -= floorf(p->phase);

        p->write = (p->write + 1) & (AT_PITCH_SHIFT_CAPACITY - 1);
    }
}
