// ═══════════════════════════════════════════════════════════════════════════
//  mixer/channel_strip/channel_strip.h — fader + pan + mute of one channel
//
//  Takes a finished channel signal and SUMS it into a stereo bus. Volume and
//  pan changes are smoothed per sample (~10 ms) so automation never zippers.
//  Pan uses a balance law: center = both sides at full volume, turning it only
//  attenuates the opposite side.
//
//  Threading: volume / pan / mute are targets written by the audio thread
//  (through engine commands). `peak` may be read from any thread.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    float volume;           // linear, 0..2 (target)
    float pan;              // -1 = left, 0 = center, +1 = right
    bool  mute;
    float gain_l;           // smoothed gains actually applied
    float gain_r;
    float smooth_coef;
    volatile float peak;    // post-fader peak of the last block (for meters)
} AT_ChannelStrip;

void channel_strip_init(AT_ChannelStrip* s, float sample_rate);

// bus += in * fader/pan. `in` may be mono (channels == 1) or stereo; `bus` is stereo.
void channel_strip_mix(AT_ChannelStrip* s, const AT_Buffer* in, AT_Buffer* bus);
