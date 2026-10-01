// ═══════════════════════════════════════════════════════════════════════════
//  io/wav_writer/wav_writer.h — streams AT_Buffers into a 16-bit PCM WAV file
//
//  For offline rendering and for recording: open, write block after block,
//  close (close patches the sizes into the header).
//
//  Does file I/O → never call wav_writer_write from the audio thread.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdio.h>

#include "core/at_buffer.h"

typedef struct {
    FILE*    file;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t frames_written;
} AT_WavWriter;

bool wav_writer_open (AT_WavWriter* w, const char* path, uint32_t sample_rate, uint32_t channels);
bool wav_writer_write(AT_WavWriter* w, const AT_Buffer* buf);   // buf->frames <= AT_MAX_BLOCK_FRAMES
void wav_writer_close(AT_WavWriter* w);
