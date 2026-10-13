// ═══════════════════════════════════════════════════════════════════════════
//  effects/pitch_shift/pitch_shift.h — change the pitch without changing the speed
//
//  Granular delay-line shifter. The sound is written into a ring buffer and
//  read back by two taps that sweep through it at a different speed than it
//  was written; each tap fades in and out with a sine² window, and the two
//  windows are half a cycle apart so their gains always add up to 1:
//
//      ratio 2.0 (octave up)    taps read twice as fast  → delay shrinks
//      ratio 0.5 (octave down)  taps read half as fast   → delay grows
//
//  This is cheap and works on anything (mixes, songs, voices). The shift itself
//  is exact (440 Hz up 12 semitones measures 880.00 Hz), but the grains are not
//  phase-aligned, which has a price:
//    - music (many partials) hides it: a slight flutter, worse on large shifts;
//    - a single steady tone can flutter strongly: depending on its frequency
//      the two overlapping grains partly cancel, and the level wobbles at the
//      grain rate |1 - ratio| / window. A pure 660 Hz sine pitched down by a
//      fifth with the default window measurably loses its main peak to
//      sidebands about 7 Hz away.
//  Sustained solo tones are better served by tape-style pitching (the sampler's
//  rate), which is exact but changes the speed with the pitch.
//
//    ratio      frequency ratio: 2^(semitones/12). 1.0 passes the sound through
//               unchanged and costs almost nothing. Engaging or leaving it
//               crossfades over ~30 ms, because the shifted sound lags the dry
//               one by up to one window (the effect's latency).
//    window_ms  grain size (default 50 ms): bigger = smoother for low sounds,
//               smaller = less echo smear on percussive sounds.
//
//      static AT_PitchShift shifter;
//      pitch_shift_init(&shifter, sample_rate);
//      engine_set_hook(&engine, AT_HOOK_SOURCE, channel, 0, pitch_shift_process, &shifter);
//      engine_set_param(&engine, pitch_shift_set_param, &shifter, AT_PITCH_SHIFT_PARAM_SEMITONES, 3.0f);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

#define AT_PITCH_SHIFT_CAPACITY  8192       // frames per channel; the grain window must fit in half

typedef struct {
    float    sample_rate;
    float    ratio;                         // target frequency ratio, 1 = unchanged
    float    window_ms;

    // ── state ──
    float    current_ratio;                 // glides to `ratio`, so a change is not a click
    float    wet;                           // 0 = dry, 1 = shifted; crossfades when the effect is engaged
    float    glide_coef;
    float    phase;                         // 0..1, position within the grain cycle
    uint32_t write;
    float    line[AT_MAX_CHANNELS][AT_PITCH_SHIFT_CAPACITY];
} AT_PitchShift;

typedef enum {
    AT_PITCH_SHIFT_PARAM_SEMITONES,         // -24 .. +24
    AT_PITCH_SHIFT_PARAM_RATIO,             // 0.25 .. 4, an exact frequency ratio
    AT_PITCH_SHIFT_PARAM_WINDOW_MS          // 10 .. 100
} AT_PitchShiftParam;

void pitch_shift_init(AT_PitchShift* p, float sample_rate);

void pitch_shift_process  (AT_Buffer* buf, void* user);          // AT_ProcessFn, user = AT_PitchShift*
void pitch_shift_set_param(void* self, int param, float value);  // AT_ParamFn
