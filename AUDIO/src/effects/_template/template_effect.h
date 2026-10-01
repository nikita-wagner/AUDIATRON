// ═══════════════════════════════════════════════════════════════════════════
//  effects/_template/template_effect.h — COPY THIS FOLDER to start an effect
//
//  1. Copy effects/_template/ to effects/<your_effect>/ and rename both files
//     (file names must be unique in the whole project).
//  2. Rename AT_TemplateEffect / template_effect_* to your effect's name.
//  3. Put your parameters and your memory into the struct, your DSP into
//     <your_effect>_process().
//  4. Register it in app/app_effects.c (one line) to get a hotkey, a status
//     label and a --fx flag. build.bat picks the new folder up by itself.
//
//  Rules for _process (it runs on the audio thread):
//    • no malloc/free, no printf, no file I/O, no locks, no Sleep
//    • all memory it needs lives in the struct, set up in _init
//    • it may read and write every sample of buf->ch[c][0 .. buf->frames-1]
//    • buf->sample_rate and buf->frame_clock tell you rate and absolute time
//
//  An effect only includes core/ headers — never the engine, the mixer or
//  another effect. That is what keeps it reusable.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    // parameters — what the user controls
    float amount;                           // example: output gain 0..2
    // state — memory the effect keeps between blocks (audio thread only)
    float last_sample[AT_MAX_CHANNELS];     // example: previous output per channel
} AT_TemplateEffect;

typedef enum {
    AT_TEMPLATE_EFFECT_PARAM_AMOUNT
} AT_TemplateEffectParam;

void template_effect_init     (AT_TemplateEffect* fx, float amount);
void template_effect_process  (AT_Buffer* buf, void* user);             // AT_ProcessFn
void template_effect_set_param(void* self, int param, float value);     // AT_ParamFn
