// ═══════════════════════════════════════════════════════════════════════════
//  threads/audio/audio_thread_init.c — thread lifetime + per-thread setup
//
//  Nothing in here renders audio. It creates the thread, prepares the thread
//  for real-time work, runs the loop (audio_thread_loop.c) and tears down.
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/audio/audio_thread.h"

#include <avrt.h>

#include "core/at_math.h"

// ─── Runs ON the new thread ──────────────────────────────────────────────────

static DWORD WINAPI audio_thread_entry(LPVOID param) {
    AT_AudioThread* t = (AT_AudioThread*)param;

    // 1. This thread calls into the device, so it needs COM.
    bool com_token = backend_thread_attach();

    // 2. MMCSS: the Windows scheduler class for pro audio. Much stronger than
    //    SetThreadPriority — this is what DAWs use.
    DWORD  task_index = 0;
    HANDLE mmcss      = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    // 3. Denormals off for every DSP module that runs on this thread.
    at_enable_flush_to_zero();

    audio_thread_loop(t);

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    backend_thread_detach(com_token);
    return 0;
}

// ─── Called from the control thread ──────────────────────────────────────────

bool audio_thread_start(AT_AudioThread* t, AT_Backend* backend, AT_ProcessFn render, void* user) {
    memset(t, 0, sizeof *t);
    t->backend     = backend;
    t->render      = render;
    t->render_user = user;

    t->handle = CreateThread(NULL, 0, audio_thread_entry, t, 0, NULL);
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
