// ═══════════════════════════════════════════════════════════════════════════
//  threads/audio/audio_thread_init.c — thread lifetime + per-thread setup
//
//  Nothing in here renders audio. It creates the thread, prepares the thread
//  for real-time work, runs the loop (audio_thread_loop.c) and tears down.
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/audio/audio_thread.h"

#include "core/at_math.h"

// ─── Runs ON the new thread ──────────────────────────────────────────────────

static DWORD WINAPI audio_thread_entry(LPVOID param) {
    AT_AudioThread* t = (AT_AudioThread*)param;

    // 1. Whatever the backend needs on a thread that talks to the device
    //    (WASAPI: COM. WinMM: nothing).
    bool backend_token = backend_thread_attach(t->backend);

    // 2. Real-time priority, so the UI or a busy disk cannot starve the audio.
    void* priority_token = audio_thread_priority_enter();

    // 3. Denormals off for every DSP module that runs on this thread.
    at_enable_flush_to_zero();

    audio_thread_loop(t);

    audio_thread_priority_leave(priority_token);
    backend_thread_detach(t->backend, backend_token);
    return 0;
}

// ─── Called from the control thread ──────────────────────────────────────────

bool audio_thread_start(AT_AudioThread* t, AT_Backend* backend, AT_ProcessFn render, void* user) {
    DWORD thread_id = 0;            // Windows 95/98 require the out-parameter

    memset(t, 0, sizeof *t);
    t->backend     = backend;
    t->render      = render;
    t->render_user = user;

    t->handle = CreateThread(NULL, 0, audio_thread_entry, t, 0, &thread_id);
    return t->handle != NULL;
}

void audio_thread_stop(AT_AudioThread* t) {
    if (!t->handle) return;

    InterlockedExchange(&t->stop, 1);
    backend_wake(t->backend);                   // the loop may be waiting for the device
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    t->handle = NULL;
}
