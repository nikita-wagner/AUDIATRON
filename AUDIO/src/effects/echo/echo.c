#include "effects/echo/echo.h"

#include "core/at_math.h"

#define GLIDE_SECONDS  0.15f        // how long a time change takes
#define DAMP_MIN_HZ    1200.0f      // lowpass corner at full damping
#define DAMP_MAX_HZ    18000.0f     // ... and at none

static float max_delay_frames(const AT_Echo* e) {
    (void)e;
    return (float)(AT_ECHO_CAPACITY - 4);
}

static float time_to_frames(const AT_Echo* e) {
    float frames = e->time_ms * 0.001f * e->sample_rate;
    return at_clampf(frames, 1.0f, max_delay_frames(e));
}

void echo_clear(AT_Echo* e) {
    memset(e->line, 0, sizeof e->line);
    memset(e->damp_state, 0, sizeof e->damp_state);
    e->write = 0;
}

void echo_init(AT_Echo* e, float sample_rate, float time_ms, float feedback, float mix) {
    memset(e, 0, sizeof *e);
    e->sample_rate = sample_rate;
    e->time_ms     = time_ms;
    e->feedback    = at_clampf(feedback, 0.0f, AT_ECHO_MAX_FEEDBACK);
    e->mix         = at_clampf(mix, 0.0f, 1.0f);
    e->damping     = 0.3f;
    e->glide_coef  = (double)at_smoothing_coef(GLIDE_SECONDS, sample_rate);
    e->delay_frames = time_to_frames(e);        // start settled: no glide from zero
}

int echo_max_ms_at(float sample_rate) {
    return (int)((float)(AT_ECHO_CAPACITY - 4) * 1000.0f / sample_rate);
}

int echo_max_ms(const AT_Echo* e) {
    return echo_max_ms_at(e->sample_rate);
}

int echo_time_ms(const AT_Echo* e) {
    return (int)(e->time_ms + 0.5f);
}

void echo_set_time_ms(AT_Echo* e, int ms) {
    echo_set_param(e, AT_ECHO_PARAM_TIME, (float)ms);
}

void echo_set_param(void* self, int param, float value) {
    AT_Echo* e = (AT_Echo*)self;

    switch ((AT_EchoParam)param) {
    case AT_ECHO_PARAM_TIME:      e->time_ms   = at_clampf(value, 1.0f, (float)echo_max_ms(e));   break;
    case AT_ECHO_PARAM_FEEDBACK:  e->feedback  = at_clampf(value, 0.0f, AT_ECHO_MAX_FEEDBACK);    break;
    case AT_ECHO_PARAM_MIX:       e->mix       = at_clampf(value, 0.0f, 1.0f);                    break;
    case AT_ECHO_PARAM_DAMPING:   e->damping   = at_clampf(value, 0.0f, 1.0f);                    break;
    case AT_ECHO_PARAM_PING_PONG: e->ping_pong = value != 0.0f;                                   break;
    }
}

// Linear interpolation between the two samples around `write - delay`.
static inline float read_line(const float* line, uint32_t write, float delay) {
    float    pos   = (float)write - delay;
    float    floor_ = floorf(pos);
    float    frac  = pos - floor_;
    int64_t  index = (int64_t)floor_;

    uint32_t a = (uint32_t)(index & (AT_ECHO_CAPACITY - 1));        // capacity is a power of two
    uint32_t b = (uint32_t)((index + 1) & (AT_ECHO_CAPACITY - 1));
    return line[a] + frac * (line[b] - line[a]);
}

_Static_assert((AT_ECHO_CAPACITY & (AT_ECHO_CAPACITY - 1)) == 0, "AT_ECHO_CAPACITY must be a power of two");

void echo_process(AT_Buffer* buf, void* user) {
    AT_Echo* e = (AT_Echo*)user;

    const uint32_t channels = buf->channels > AT_MAX_CHANNELS ? AT_MAX_CHANNELS : buf->channels;
    if (channels == 0) return;

    // damping: lowpass corner from 18 kHz (none) down to 1.2 kHz (full)
    float corner = DAMP_MAX_HZ * powf(DAMP_MIN_HZ / DAMP_MAX_HZ, e->damping);
    if (corner > e->sample_rate * 0.45f) corner = e->sample_rate * 0.45f;
    const float damp_k = e->damping <= 0.001f ? 1.0f                              // no damping: pass straight through
                       : 1.0f - expf(-(float)AT_TWO_PI * corner / e->sample_rate);

    const float target = time_to_frames(e);

    for (uint32_t i = 0; i < buf->frames; i++) {
        e->delay_frames += ((double)target - e->delay_frames) * e->glide_coef;

        float echoed[AT_MAX_CHANNELS];
        for (uint32_t c = 0; c < channels; c++) {
            echoed[c] = read_line(e->line[c], e->write, (float)e->delay_frames);
            e->damp_state[c] += (echoed[c] - e->damp_state[c]) * damp_k;       // darker with every repeat
        }

        if (e->ping_pong && channels == 2) {
            // the input enters the left line only; the repeats cross over every time
            float in = 0.5f * (buf->ch[0][i] + buf->ch[1][i]);
            e->line[0][e->write] = in + e->feedback * e->damp_state[1];
            e->line[1][e->write] =      e->feedback * e->damp_state[0];
        } else {
            for (uint32_t c = 0; c < channels; c++)
                e->line[c][e->write] = buf->ch[c][i] + e->feedback * e->damp_state[c];
        }

        for (uint32_t c = 0; c < channels; c++)
            buf->ch[c][i] += e->mix * echoed[c];

        e->write = (e->write + 1) & (AT_ECHO_CAPACITY - 1);
    }
}
