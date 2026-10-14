// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine_source.c — the machine's sound (audio thread)
//
//  One read head loops over the part of the file the curves give for the
//  current (smoothed) throttle. Whenever it has to jump, a second head keeps
//  playing where the first one was and fades out while the first fades in
//  (equal power):
//
//    at the loop seam      the jump starts xfade_ms before the end and lands
//                          xfade_ms before the start, so after the fade the
//                          head is exactly at `from` and the old one at `to`
//    edges moved past it   the throttle made the loop shorter under the head
//                          (or moved it away): a 12 ms fade to the new start
//
//  Then the machine's own effects, those that are on: EQ (low shelf, bell,
//  high shelf, low-pass) → crush → tremolo → echo. Every one of their settings
//  is a curve on the throttle too.
//
//  The throttle is followed with a one-pole smoother; the curves are evaluated
//  every 32 frames and the gain is ramped in between, so nothing zippers. The
//  tremolo keeps its own phase, so a rate that follows the throttle never
//  jumps. After the machine stops, the effects run on for a few seconds so an
//  echo rings out.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <math.h>
#include <string.h>

#include "core/at_math.h"

#define SUB_BLOCK     32
#define RUN_FADE_S    0.03f         // on / off fade
#define JUMP_MS       12.0f         // fade when the loop's edges move past the head
#define TAIL_S        3.0f          // effects keep running this long after the machine stopped
#define HALF_PI       1.5707963f
#define TWO_PI        6.283185307179586

// Cubic (Hermite) read of one channel at a fractional frame; silence outside the file.
static float read_at(const AT_Clip* c, double pos, uint32_t channel) {
    const int64_t  frames = (int64_t)c->frames;
    const uint32_t chans  = c->channels ? c->channels : 1;
    if (channel >= chans) channel = chans - 1;
    const int64_t i = (int64_t)floor(pos);
    if (i < -1 || i > frames) return 0.0f;
    const float f = (float)(pos - (double)i);

    float x[4];
    for (int k = 0; k < 4; k++) {
        const int64_t at = i - 1 + k;
        x[k] = (at < 0 || at >= frames) ? 0.0f : c->data[at * chans + channel] * (1.0f / 32768.0f);
    }
    const float c1 = 0.5f * (x[2] - x[0]);
    const float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
    return ((c3 * f + c2) * f + c1) * f + x[1];
}

static float curve(const AT_MachineSound* s, AT_MachineCurveId id, float t) {
    return machine_curve_eval(&s->curves[id], t);
}

// The looped part at throttle t, in frames of the clip; always inside the file and not too short.
static void loop_region(const AT_MachineSound* s, const AT_Clip* c, float t, double* from, double* to) {
    const double length = (double)c->frames / (double)c->sample_rate;
    const double shortest = AT_MACHINE_MIN_LOOP_S < length ? AT_MACHINE_MIN_LOOP_S : length;
    double a = curve(s, AT_MACHINE_CURVE_FROM, t);
    double b = curve(s, AT_MACHINE_CURVE_TO, t);
    if (b < a) { double x = a; a = b; b = x; }
    if (b > length) b = length;
    if (a < 0.0) a = 0.0;
    if (b - a < shortest) {
        b = a + shortest;
        if (b > length) { b = length; a = length - shortest; }
    }
    *from = a * c->sample_rate;
    *to   = b * c->sample_rate;
}

static void start_jump(AT_MachineSound* s, double target, uint32_t frames) {
    s->head[1]   = s->head[0];
    s->head[0]   = target;
    s->jump_len  = frames ? frames : 1;
    s->jump_left = s->jump_len;
}

// The machine's own effects on `n` frames of `out` from `offset`, set from throttle t.
static void apply_fx(AT_MachineSound* s, AT_Buffer* out, uint32_t offset, uint32_t n, float t, float rate_out) {
    AT_Buffer slice = at_buffer_slice(out, offset, n);

    if (s->fx_on[AT_MACHINE_FX_EQ]) {
        const float nyquist = rate_out * 0.45f;
        const AT_BiquadCoeffs k[4] = {
            biquad_design(AT_FILTER_LOWSHELF, rate_out, 120.0f, 0.707f, at_clampf(curve(s, AT_MACHINE_CURVE_EQ_LOW, t), -24.0f, 24.0f)),
            biquad_design(AT_FILTER_PEAK, rate_out, at_clampf(curve(s, AT_MACHINE_CURVE_EQ_MID_FREQ, t), 20.0f, nyquist), 1.0f,
                          at_clampf(curve(s, AT_MACHINE_CURVE_EQ_MID_GAIN, t), -24.0f, 24.0f)),
            biquad_design(AT_FILTER_HIGHSHELF, rate_out, 6000.0f < nyquist ? 6000.0f : nyquist, 0.707f,
                          at_clampf(curve(s, AT_MACHINE_CURVE_EQ_HIGH, t), -24.0f, 24.0f)),
            biquad_design(AT_FILTER_LOWPASS, rate_out, at_clampf(curve(s, AT_MACHINE_CURVE_EQ_LOWPASS, t), 20.0f, nyquist), 0.707f, 0.0f),
        };
        for (uint32_t ch = 0; ch < slice.channels; ch++)
            for (int b = 0; b < 4; b++)
                biquad_process(&k[b], &s->eq_state[ch][b], slice.ch[ch], n);
    }

    if (s->fx_on[AT_MACHINE_FX_CRUSH]) {
        bitcrusher_set_param(&s->crush, AT_BITCRUSHER_PARAM_DOWNSAMPLE, at_clampf(curve(s, AT_MACHINE_CURVE_CRUSH_DOWNSAMPLE, t), 1.0f, 64.0f));
        bitcrusher_set_param(&s->crush, AT_BITCRUSHER_PARAM_BITS, at_clampf(curve(s, AT_MACHINE_CURVE_CRUSH_BITS, t), 1.0f, 16.0f));
        bitcrusher_process(&slice, &s->crush);
    }

    if (s->fx_on[AT_MACHINE_FX_TREMOLO]) {
        const float  depth = at_clampf(curve(s, AT_MACHINE_CURVE_TREMOLO_DEPTH, t), 0.0f, 1.0f);
        const double step  = TWO_PI * at_clampf(curve(s, AT_MACHINE_CURVE_TREMOLO_RATE, t), 0.0f, 80.0f) / rate_out;
        for (uint32_t i = 0; i < n; i++) {
            const float g = 1.0f - depth * 0.5f * (1.0f - (float)cos(s->tremolo_phase));
            for (uint32_t ch = 0; ch < slice.channels; ch++) slice.ch[ch][i] *= g;
            s->tremolo_phase += step;
        }
        if (s->tremolo_phase > TWO_PI) s->tremolo_phase = fmod(s->tremolo_phase, TWO_PI);
    }

    if (s->fx_on[AT_MACHINE_FX_ECHO] && s->echo) {
        echo_set_param(s->echo, AT_ECHO_PARAM_TIME,     curve(s, AT_MACHINE_CURVE_ECHO_TIME, t));     // glides, no jump
        echo_set_param(s->echo, AT_ECHO_PARAM_FEEDBACK, curve(s, AT_MACHINE_CURVE_ECHO_FEEDBACK, t));
        echo_set_param(s->echo, AT_ECHO_PARAM_MIX,      curve(s, AT_MACHINE_CURVE_ECHO_MIX, t));
        echo_process(&slice, s->echo);
    }
}

static void machine_render(AT_Buffer* out, void* self) {
    AT_MachineSound* s = (AT_MachineSound*)self;
    const AT_Clip*   c = s->slots ? &s->slots[s->slot] : NULL;
    s->shown_running = s->running;

    bool any_fx = false;
    for (int fx = 0; fx < AT_MACHINE_FX_COUNT; fx++) any_fx |= s->fx_on[fx];
    const bool has_clip = c && c->data && c->frames >= 4 && c->sample_rate;
    const bool reading  = has_clip && (s->running || s->run_env > 0.0f);
    if (!reading) {
        s->started = false;
        if (!any_fx || s->tail_left == 0) return;
    }

    const float rate_out = out->sample_rate ? (float)out->sample_rate : s->sample_rate;
    if (reading) s->tail_left = (uint32_t)(TAIL_S * rate_out);
    else         s->tail_left = s->tail_left > out->frames ? s->tail_left - out->frames : 0;

    const float  run_step    = 1.0f / (RUN_FADE_S * rate_out);
    const float  smooth      = 1.0f - expf(-1.0f / ((s->smooth_ms > 1.0f ? s->smooth_ms : 1.0f) * 0.001f * rate_out));
    const double clip_to_out = has_clip ? (double)c->sample_rate / rate_out : 1.0;
    const uint32_t jump_frames = (uint32_t)(JUMP_MS * 0.001f * rate_out);

    double from = 0, to = 0;
    for (uint32_t done = 0; done < out->frames; ) {
        const uint32_t n = out->frames - done < SUB_BLOCK ? out->frames - done : SUB_BLOCK;

        // where the throttle is at the end of this piece
        s->throttle += (s->throttle_target - s->throttle) * (1.0f - powf(1.0f - smooth, (float)n));
        const float t = s->throttle;

        if (reading) {
            float gain = curve(s, AT_MACHINE_CURVE_GAIN, t) * s->volume;
            if (gain < 0.0f) gain = 0.0f;
            loop_region(s, c, t, &from, &to);
            double rate = curve(s, AT_MACHINE_CURVE_RATE, t);
            rate = (rate < 0.05 ? 0.05 : rate > 8.0 ? 8.0 : rate) * clip_to_out;

            // the seam's crossfade: at most half the loop, and there must be file before `from` to fade in
            double xfade = s->xfade_ms * 0.001 * c->sample_rate;
            if (xfade > (to - from) * 0.5) xfade = (to - from) * 0.5;
            if (xfade > from) xfade = from;
            if (xfade < 1.0) xfade = 0.0;

            if (!s->started) {
                s->head[0]   = from;
                s->jump_left = 0;
                s->started   = true;
                // on a synced track: where the loop would be had it started with the sync clock
                if (engine_channel_synced(s->engine, s->channel) && to > from) {
                    const double elapsed = engine_sync_join(s->engine, s->channel);
                    s->head[0] = from + fmod(elapsed * rate, to - from);
                }
            }

            const float g0 = s->gain, gstep = (gain - g0) / (float)n;
            for (uint32_t k = 0; k < n; k++) {
                if (s->jump_left == 0) {
                    if (s->head[0] >= to - xfade) {
                        if (xfade > 0.0 && s->head[0] < to)
                            start_jump(s, from - xfade + (s->head[0] - (to - xfade)), (uint32_t)(xfade / rate));
                        else
                            start_jump(s, from, jump_frames);           // the loop got shorter under the head
                    } else if (s->head[0] < from - xfade - 1.0) {
                        start_jump(s, from, jump_frames);               // the loop moved away from the head
                    }
                }

                const float target_env = s->running ? 1.0f : 0.0f;
                if (s->run_env < target_env)      s->run_env = s->run_env + run_step > 1.0f ? 1.0f : s->run_env + run_step;
                else if (s->run_env > target_env) s->run_env = s->run_env - run_step < 0.0f ? 0.0f : s->run_env - run_step;

                float wa = 1.0f, wb = 0.0f;
                if (s->jump_left) {
                    const float w = 1.0f - (float)s->jump_left / (float)s->jump_len;
                    wa = sinf(w * HALF_PI);
                    wb = cosf(w * HALF_PI);
                }
                const float g = (g0 + gstep * (float)(k + 1)) * s->run_env;
                const uint32_t i = done + k;
                for (uint32_t ch = 0; ch < out->channels; ch++) {
                    float v = wa * read_at(c, s->head[0], ch);
                    if (wb > 0.0f) v += wb * read_at(c, s->head[1], ch);
                    out->ch[ch][i] += v * g;
                }

                s->head[0] += rate;
                if (s->jump_left) { s->head[1] += rate; s->jump_left--; }
            }
            s->gain = gain;
        }

        if (any_fx) apply_fx(s, out, done, n, t, rate_out);
        done += n;
    }

    s->shown_throttle = s->throttle;
    if (reading) {
        s->shown_head_s = (float)(s->head[0] / c->sample_rate);
        s->shown_from_s = (float)(from / c->sample_rate);
        s->shown_to_s   = (float)(to / c->sample_rate);
    }
    if (!s->running && s->run_env <= 0.0f) s->started = false;      // the next start begins at the loop's start
}

static bool machine_playing(void* self) {
    return ((const AT_MachineSound*)self)->running;
}

static void machine_all_notes_off(void* self) {
    ((AT_MachineSound*)self)->running = false;                      // Space / panic: the machine stops too
}

void machine_set_param(void* self, int param, float value) {
    AT_MachineSound* s = (AT_MachineSound*)self;
    switch (param) {
    case AT_MACHINE_PARAM_THROTTLE:  s->throttle_target = at_clampf(value, 0.0f, 1.0f); return;
    case AT_MACHINE_PARAM_RUNNING:   s->running = value > 0.5f; return;
    case AT_MACHINE_PARAM_VOLUME:    s->volume = at_clampf(value, 0.0f, 4.0f); return;
    case AT_MACHINE_PARAM_SMOOTH_MS: s->smooth_ms = at_clampf(value, 1.0f, 5000.0f); return;
    case AT_MACHINE_PARAM_XFADE_MS:  s->xfade_ms = at_clampf(value, 0.0f, 1000.0f); return;
    case AT_MACHINE_PARAM_CLIP:
        s->slot    = (int)value & 1;
        s->started = false;
        s->run_env = 0.0f;                                          // the new file fades in from silence
        return;
    case AT_MACHINE_PARAM_FX_EQ:
    case AT_MACHINE_PARAM_FX_CRUSH:
    case AT_MACHINE_PARAM_FX_TREMOLO:
    case AT_MACHINE_PARAM_FX_ECHO: {
        const int  fx = param - AT_MACHINE_PARAM_FX_EQ;
        const bool on = value > 0.5f;
        if (on && !s->fx_on[fx]) {                                  // a fresh start: no old filter memory or echo tail
            if (fx == AT_MACHINE_FX_EQ) memset(s->eq_state, 0, sizeof s->eq_state);
            if (fx == AT_MACHINE_FX_ECHO && s->echo) echo_clear(s->echo);
        }
        s->fx_on[fx] = on && (fx != AT_MACHINE_FX_ECHO || s->echo);
        return;
    }
    default: break;
    }
    if (param < AT_MACHINE_PARAM_CURVE_BASE || param >= AT_MACHINE_PARAM_COUNT) return;
    const int curve_id = (param - AT_MACHINE_PARAM_CURVE_BASE) / AT_MACHINE_FIELD_STRIDE;
    const int field    = (param - AT_MACHINE_PARAM_CURVE_BASE) % AT_MACHINE_FIELD_STRIDE;
    AT_MachineCurve* c = &s->curves[curve_id];
    switch (field) {
    case AT_MACHINE_FIELD_IDLE:   c->idle = value; break;
    case AT_MACHINE_FIELD_FULL:   c->full = value; break;
    case AT_MACHINE_FIELD_SHAPE:  c->shape = (AT_MachineShape)((int)value < 0 ? 0 : (int)value >= AT_MACHINE_SHAPES ? AT_MACHINE_SHAPES - 1 : (int)value); break;
    case AT_MACHINE_FIELD_AMOUNT: c->amount = at_clampf(value, 1.0f, 6.0f); break;
    case AT_MACHINE_FIELD_POINT1:
    case AT_MACHINE_FIELD_POINT2:
    case AT_MACHINE_FIELD_POINT3: c->points[field - AT_MACHINE_FIELD_POINT1] = at_clampf(value, 0.0f, 1.0f); break;
    default: break;
    }
}

const AT_SourceType AT_MACHINE_SOURCE = {
    .name          = "machine",
    .render        = machine_render,
    .note_on       = NULL,
    .note_off      = NULL,
    .all_notes_off = machine_all_notes_off,
    .playing       = machine_playing
};
