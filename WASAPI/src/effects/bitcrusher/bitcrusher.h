// ═══════════════════════════════════════════════════════════════════════════
//  effects/bitcrusher/bitcrusher.h — sample-rate + bit-depth reduction
//
//  Holds every Nth sample (lo-fi aliasing) and rounds it to a coarse grid
//  (quantization noise). Works on any buffer, mono or stereo.
//
//      static AT_Bitcrusher crusher;
//      bitcrusher_init(&crusher, 6, 5.0f);
//      engine_set_hook(&engine, AT_HOOK_MASTER_POST, 0, 0, bitcrusher_process, &crusher);
//
//  Live parameter change from the control thread:
//      engine_set_param(&engine, bitcrusher_set_param, &crusher, AT_BITCRUSHER_PARAM_BITS, 8.0f);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef struct {
    // parameters
    uint32_t downsample;                // hold every Nth sample (1 = no rate reduction)
    float    levels;                    // quantization steps per unit = 2^(bits-1)
    // state (audio thread)
    uint32_t counter;
    float    held[AT_MAX_CHANNELS];
} AT_Bitcrusher;

typedef enum {
    AT_BITCRUSHER_PARAM_DOWNSAMPLE,     // 1..N
    AT_BITCRUSHER_PARAM_BITS            // 1..16
} AT_BitcrusherParam;

void bitcrusher_init     (AT_Bitcrusher* bc, uint32_t downsample, float bits);
void bitcrusher_process  (AT_Buffer* buf, void* user);              // AT_ProcessFn, user = AT_Bitcrusher*
void bitcrusher_set_param(void* self, int param, float value);      // AT_ParamFn
