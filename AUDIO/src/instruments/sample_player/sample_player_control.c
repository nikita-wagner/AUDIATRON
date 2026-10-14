// ═══════════════════════════════════════════════════════════════════════════
//  instruments/sample_player/sample_player_control.c — pitch, speed, transport
//
//  One rule decides everything the audio thread is told:
//
//      tape mode:         sampler rate = 2^(pitch/12) * speed      shifter ratio = 1
//      keep-tempo mode:   sampler rate = speed                     shifter ratio = 2^(pitch/12) / speed
//
//  In keep-tempo mode the sampler's rate change moves the pitch by `speed`;
//  the shifter takes that out again and applies the wanted pitch, so the net
//  pitch is 2^(pitch/12) and the tempo is `speed`.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/sample_player/sample_player.h"

#include <math.h>

#include "core/at_math.h"

// Sends one sampler / shifter parameter: directly before attach, through the
// engine afterwards. (The audio thread is not running before attach.)
static void set_sampler(AT_SamplePlayer* p, AT_SamplerParam id, float value) {
    if (p->engine) engine_set_param(p->engine, sampler_set_param, &p->sampler, (int)id, value);
    else           sampler_set_param(&p->sampler, (int)id, value);
}

static void set_shifter(AT_SamplePlayer* p, float ratio) {
    if (p->engine) engine_set_param(p->engine, pitch_shift_set_param, &p->shifter, AT_PITCH_SHIFT_PARAM_RATIO, ratio);
    else           pitch_shift_set_param(&p->shifter, AT_PITCH_SHIFT_PARAM_RATIO, ratio);
}

// Recomputes both from pitch / speed / mode and sends them.
static void apply_pitch_and_speed(AT_SamplePlayer* p) {
    float pitch_ratio = powf(2.0f, p->pitch / 12.0f);

    if (p->keep_tempo) {
        set_sampler(p, AT_SAMPLER_PARAM_RATE, p->speed);
        set_shifter(p, pitch_ratio / p->speed);
    } else {
        set_sampler(p, AT_SAMPLER_PARAM_RATE, pitch_ratio * p->speed);
        set_shifter(p, 1.0f);
    }
}

void sample_player_set_pitch(AT_SamplePlayer* p, float semitones) {
    p->pitch = at_clampf(semitones, AT_PLAYER_PITCH_MIN, AT_PLAYER_PITCH_MAX);
    apply_pitch_and_speed(p);
}

void sample_player_set_speed(AT_SamplePlayer* p, float speed) {
    p->speed = at_clampf(speed, AT_PLAYER_SPEED_MIN, AT_PLAYER_SPEED_MAX);
    apply_pitch_and_speed(p);
}

void sample_player_set_keep_tempo(AT_SamplePlayer* p, bool keep_tempo) {
    p->keep_tempo = keep_tempo;
    apply_pitch_and_speed(p);
}

void sample_player_set_loop(AT_SamplePlayer* p, bool loop) {
    p->loop = loop;
    set_sampler(p, AT_SAMPLER_PARAM_LOOP, loop ? 1.0f : 0.0f);
}

bool sample_player_set_cut(AT_SamplePlayer* p, float from, float to) {
    if (!(from >= 0.0f && from < to && to <= 1.0f)) return false;      // also rejects NaN
    set_sampler(p, AT_SAMPLER_PARAM_REGION_FROM, from);
    set_sampler(p, AT_SAMPLER_PARAM_REGION_TO,   to);
    return true;
}

void sample_player_play (AT_SamplePlayer* p) { set_sampler(p, AT_SAMPLER_PARAM_PLAY,  0.0f); }
void sample_player_play_now(AT_SamplePlayer* p) { set_sampler(p, AT_SAMPLER_PARAM_PLAY, 1.0f); }
void sample_player_pause(AT_SamplePlayer* p) { set_sampler(p, AT_SAMPLER_PARAM_PAUSE, 1.0f); }
void sample_player_stop (AT_SamplePlayer* p) { set_sampler(p, AT_SAMPLER_PARAM_STOP,  0.0f); }

void sample_player_seek_seconds(AT_SamplePlayer* p, double seconds) {
    double length = audio_clip_seconds(&p->clip);
    if (seconds > length) seconds = length;                 // the sampler also clamps; this keeps the value sane
    if (seconds < 0.0)    seconds = 0.0;
    set_sampler(p, AT_SAMPLER_PARAM_SEEK, (float)seconds);
}

double sample_player_position_seconds(const AT_SamplePlayer* p) {
    return p->clip.sample_rate ? (double)p->sampler.position_frames / (double)p->clip.sample_rate : 0.0;
}

bool sample_player_is_playing(const AT_SamplePlayer* p) {
    return p->sampler.playing != 0;
}
