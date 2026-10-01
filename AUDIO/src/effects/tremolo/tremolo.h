// ═══════════════════════════════════════════════════════════════════════════
//  effects/tremolo/tremolo.h — periodic volume modulation
//
//  The LFO is derived from buf->frame_clock, so it is locked to absolute
//  sample time and stays continuous across blocks no matter how the device
//  chunks them. No state besides the two parameters.
//
//      static AT_Tremolo tremolo;
//      tremolo_init(&tremolo, 6.0f, 0.8f);
//      engine_set_hook(&engine, AT_HOOK_CHANNEL, channel, 0, tremolo_process, &tremolo);
//
//  Live parameter change from the control thread:
//      engine_set_param(&engine, tremolo_set_param, &tremolo, AT_TREMOLO_PARAM_RATE, 4.0f);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    float rate_hz;
    float depth;                    // 0 = no effect, 1 = dips to silence
} AT_Tremolo;

typedef enum {
    AT_TREMOLO_PARAM_RATE,          // Hz
    AT_TREMOLO_PARAM_DEPTH          // 0..1
} AT_TremoloParam;

void tremolo_init     (AT_Tremolo* t, float rate_hz, float depth);
void tremolo_process  (AT_Buffer* buf, void* user);                 // AT_ProcessFn, user = AT_Tremolo*
void tremolo_set_param(void* self, int param, float value);         // AT_ParamFn
