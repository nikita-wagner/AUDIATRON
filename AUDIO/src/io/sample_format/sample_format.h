// ═══════════════════════════════════════════════════════════════════════════
//  io/sample_format/sample_format.h — planar float → interleaved device samples
//
//  The single place where the engine's float world is turned into bytes a
//  device or a file understands. Used by the audio thread (whatever format
//  WASAPI negotiated) and by the WAV writer (int16).
//
//  Channel mapping when the destination has a different channel count:
//    1 channel   → average of left and right
//    > 2 channels → left/right on the first two, silence on the rest
//  Samples are clamped to ±1.0 before conversion.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

typedef enum {
    AT_SAMPLE_F32,
    AT_SAMPLE_S16,
    AT_SAMPLE_S24,          // packed, 3 bytes
    AT_SAMPLE_S32           // 4 bytes (also used for 24-bit-in-32 containers)
} AT_SampleType;

const char* sample_format_name (AT_SampleType t);
uint32_t    sample_format_bytes(AT_SampleType t);       // bytes per sample

// Writes src->frames frames of dst_channels interleaved samples to dst.
void sample_format_write(uint8_t* dst, AT_SampleType type, uint32_t dst_channels,
                         const AT_Buffer* src);
