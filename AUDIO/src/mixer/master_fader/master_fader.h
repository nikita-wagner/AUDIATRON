// ═══════════════════════════════════════════════════════════════════════════
//  mixer/master_fader/master_fader.h — one smoothed gain for a whole bus
//
//  `volume` is the target; the applied gain glides to it in ~10 ms so volume
//  changes never click.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    float volume;           // linear, 0..2 (target)
    float gain;             // smoothed gain actually applied
    float smooth_coef;
} AT_MasterFader;

void master_fader_init   (AT_MasterFader* f, float sample_rate);
void master_fader_process(AT_MasterFader* f, AT_Buffer* bus);    // in place
