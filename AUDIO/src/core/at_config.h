// ═══════════════════════════════════════════════════════════════════════════
//  core/at_config.h — the two limits every module shares
//
//  Module-specific limits live in the module that owns them
//  (AT_SYNTH_VOICES in synth/synth.h, AT_EQ_BANDS in equalizer/equalizer.h,
//  AT_ENGINE_CHANNELS in engine/engine.h, ...).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#define AT_MAX_CHANNELS      2       // width of every AT_Buffer inside the engine (stereo)
#define AT_MAX_BLOCK_FRAMES  1024    // largest block any module is asked to process at once
