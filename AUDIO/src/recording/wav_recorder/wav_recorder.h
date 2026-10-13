// ═══════════════════════════════════════════════════════════════════════════
//  recording/wav_recorder/wav_recorder.h — records what the engine plays into a WAV
//
//  Two halves, because the audio thread must never touch a file:
//
//    audio thread    wav_recorder_process, an ordinary effect function hooked
//                    on AT_HOOK_OUTPUT, copies every block into a lock-free
//                    ring (one writer, one reader, no locks, no allocation)
//    control thread  wav_recorder_flush, called regularly (every status tick),
//                    moves what has arrived into the file
//
//  The ring holds AT_WAV_RECORDER_FRAMES frames (~2.7 s at 48 kHz). If the
//  control thread falls further behind than that, frames are dropped and
//  counted (`dropped`), never waited for.
//
//      wav_recorder_open(&rec, "take.wav", rate);
//      engine_set_hook(&engine, AT_HOOK_OUTPUT, 0, 3, wav_recorder_process, &rec);
//      ... wav_recorder_flush(&rec) every few tens of milliseconds ...
//      engine_set_hook(&engine, AT_HOOK_OUTPUT, 0, 3, NULL, NULL);
//      (wait until the audio thread has applied that, then)
//      wav_recorder_close(&rec);           // flushes the rest, closes, frees
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/at_buffer.h"
#include "io/wav_writer/wav_writer.h"

#define AT_WAV_RECORDER_FRAMES  131072      // ring size, a power of two

typedef struct {
    float*         ring;                    // AT_WAV_RECORDER_FRAMES stereo frames, interleaved
    volatile LONG  write;                   // frames the audio thread has put in (wraps, unsigned math)
    volatile LONG  read;                    // frames the control thread has taken out
    volatile LONG  dropped;                 // frames lost because the ring was full
    volatile LONG  started;                 // 1 once the first block arrived; first_frame is valid then
    uint64_t       first_frame;             // frame_clock of the first recorded frame

    AT_WavWriter   wav;
    float          chunk[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];     // control thread: ring → file
} AT_WavRecorder;

// Control thread. Allocates the ring (~1 MB) and creates the file.
bool     wav_recorder_open   (AT_WavRecorder* r, const char* path, uint32_t sample_rate);
// Audio thread. AT_ProcessFn, user = AT_WavRecorder*; leaves the buffer unchanged.
void     wav_recorder_process(AT_Buffer* buf, void* user);
// Control thread. Writes what has arrived; returns the frames written now.
uint32_t wav_recorder_flush  (AT_WavRecorder* r);
// Control thread, once the hook is removed and no longer runs. Safe twice.
void     wav_recorder_close  (AT_WavRecorder* r);

static inline double wav_recorder_seconds(const AT_WavRecorder* r) {
    return r->wav.sample_rate ? (double)r->wav.frames_written / (double)r->wav.sample_rate : 0.0;
}
