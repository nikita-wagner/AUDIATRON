// ═══════════════════════════════════════════════════════════════════════════
//  effects/soft_clip/soft_clip.h — soft-knee clipper (the master limiter)
//
//  Fully transparent below `knee`, tanh-shaped above it, never exceeds ±1.0.
//  Stops digital clipping from sounding harsh when several loud sounds add up.
//
//  The engine runs one of these at the end of the master bus
//  (engine_set_limiter turns it on/off), but like every effect it can also be
//  plugged into any hook point:
//
//      static AT_SoftClip clip;
//      soft_clip_init(&clip, 0.8f);
//      engine_set_hook(&engine, AT_HOOK_CHANNEL, channel, 0, soft_clip_process, &clip);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    float knee;                     // 0..1: linear below this level, curved above
} AT_SoftClip;

typedef enum {
    AT_SOFT_CLIP_PARAM_KNEE         // 0..0.99
} AT_SoftClipParam;

void soft_clip_init     (AT_SoftClip* sc, float knee);
void soft_clip_process  (AT_Buffer* buf, void* user);               // AT_ProcessFn, user = AT_SoftClip*
void soft_clip_set_param(void* self, int param, float value);       // AT_ParamFn
