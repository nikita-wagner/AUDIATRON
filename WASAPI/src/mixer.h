// ═══════════════════════════════════════════════════════════════════════════
//  mixer.h — channel strips, master bus, soft-clip limiter, meters
//
//  Strip:   [source] → EQ → fader + balance → summed into master bus
//  Master:  master EQ → master fader → soft clip → device
//
//  Fader/pan changes are smoothed per sample (~10 ms) so automation never
//  zippers. The limiter is a soft-knee clipper: fully transparent below
//  SOFTCLIP_KNEE, tanh-shaped above it, never exceeds ±1.0.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "at_common.h"
#include "equalizer.h"

typedef struct {
    float        volume;        // linear, 0..2 (target)
    float        pan;           // -1 = left, 0 = center, +1 = right (balance law)
    bool         mute;
    AT_Equalizer eq;
    float        gain_l;        // smoothed gains actually applied (audio thread)
    float        gain_r;
    volatile float peak;        // post-fader peak of the last block (for meters)
} AT_MixerChannel;

typedef struct {
    AT_MixerChannel ch[AT_MAX_MIX_CHANNELS];
    float           master_volume;              // target
    float           master_gain;                // smoothed
    AT_Equalizer    master_eq;
    bool            soft_clip;
    volatile float  master_peak[AT_MAX_CHANNELS];   // post-limiter, last block
    float           smooth_coef;
    float           sample_rate;
} AT_Mixer;

void mixer_init(AT_Mixer* m, float sample_rate);

// strip → fader/pan → bus (+=). strip may be mono (channels == 1) or stereo.
void mixer_channel_to_bus(AT_Mixer* m, uint32_t index, const AT_Buffer* strip, AT_Buffer* bus);

void mixer_master_gain (AT_Mixer* m, AT_Buffer* bus);    // master fader
void mixer_master_limit(AT_Mixer* m, AT_Buffer* bus);    // soft clip + meters
