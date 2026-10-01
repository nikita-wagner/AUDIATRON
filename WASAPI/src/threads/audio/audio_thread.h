// ═══════════════════════════════════════════════════════════════════════════
//  threads/audio/audio_thread.h — the real-time thread that feeds the device
//
//  Connects a device (backend/) to something that produces audio (any
//  AT_ProcessFn — normally engine_render). It owns neither of them.
//
//  Files in this folder:
//    audio_thread_init.c   creating / stopping the thread, and what the thread
//                          sets up for itself before it loops (COM, MMCSS
//                          priority, flush-to-zero)
//    audio_thread_loop.c   the loop: wait for the device → render → convert →
//                          hand the buffer back
//
//  Start order:  audio_thread_start() → backend_start()
//  Stop order:   audio_thread_stop()  → backend_stop()
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "backend/wasapi/wasapi_backend.h"
#include "core/at_buffer.h"

typedef struct {
    AT_Backend*   backend;
    AT_ProcessFn  render;           // fills a stereo float buffer (e.g. engine_render)
    void*         render_user;

    volatile LONG overruns;         // blocks where rendering took longer than one period

    // ── internals ──
    HANDLE        handle;
    volatile LONG stop;
    uint64_t      frame_clock;
    float         scratch[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];
} AT_AudioThread;

// ─── audio_thread_init.c ─────────────────────────────────────────────────────

// The backend must be open. `render` may be NULL (plays silence).
bool audio_thread_start(AT_AudioThread* t, AT_Backend* backend, AT_ProcessFn render, void* user);

// Signals the loop to end and waits for the thread. Safe to call twice.
void audio_thread_stop(AT_AudioThread* t);

// ─── audio_thread_loop.c ─────────────────────────────────────────────────────

// Runs on the audio thread until t->stop is set or the stream dies.
void audio_thread_loop(AT_AudioThread* t);
