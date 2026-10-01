// ═══════════════════════════════════════════════════════════════════════════
//  mixer/peak_meter/peak_meter.h — per-channel peak of the last block
//
//  The audio thread measures, any thread may read. A single float store/load
//  is atomic on x86-64, which is all a level meter needs.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    volatile float peak[AT_MAX_CHANNELS];   // linear, 1.0 = full scale
} AT_PeakMeter;

void  peak_meter_init   (AT_PeakMeter* m);
void  peak_meter_measure(AT_PeakMeter* m, const AT_Buffer* buf);        // audio thread
float peak_meter_read   (const AT_PeakMeter* m, uint32_t channel);      // any thread
