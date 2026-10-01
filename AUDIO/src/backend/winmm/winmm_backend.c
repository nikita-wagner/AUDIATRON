// ═══════════════════════════════════════════════════════════════════════════
//  backend/winmm/winmm_backend.c — WinMM as an AT_Backend
//
//  The only file in this folder that deals with the backend contract
//  (backend/audio_backend.h). It turns MMRESULTs into bool + error text.
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/winmm/winmm_backend.h"

#include <stdio.h>

static bool fail(AT_WinmmBackend* b, const char* what, MMRESULT mmr) {
    char text[128] = "";
    waveOutGetErrorTextA(mmr, text, sizeof text);       // the system's own description
    snprintf(b->info.error, sizeof b->info.error, "%s failed: MMRESULT %u %s", what, (unsigned)mmr, text);
    return false;
}

// waveOut is part of every Windows since 95.
static bool adapter_available(void* self) { (void)self; return true; }

static bool adapter_enumerate(void* self, AT_DeviceEnumFn fn, void* user) {
    (void)self;
    return winmm_enumerate_devices(fn, user);
}

static bool adapter_open(void* self, const AT_BackendConfig* cfg) {
    AT_WinmmBackend* b = (AT_WinmmBackend*)self;
    MMRESULT mmr = winmm_open(b, cfg);
    return mmr == MMSYSERR_NOERROR ? true : fail(b, "opening the WinMM device", mmr);
}

static bool adapter_start(void* self) {
    AT_WinmmBackend* b = (AT_WinmmBackend*)self;
    MMRESULT mmr = winmm_start(b);
    return mmr == MMSYSERR_NOERROR ? true : fail(b, "starting the WinMM stream", mmr);
}

static void adapter_stop (void* self) { winmm_stop((AT_WinmmBackend*)self); }
static void adapter_close(void* self) { winmm_close((AT_WinmmBackend*)self); }

static bool adapter_wait(void* self, uint32_t timeout_ms) { return winmm_wait((AT_WinmmBackend*)self, timeout_ms); }
static void adapter_wake(void* self)                      { winmm_wake((AT_WinmmBackend*)self); }

static bool adapter_acquire(void* self, uint8_t** data, uint32_t* frames) {
    return winmm_acquire((AT_WinmmBackend*)self, data, frames);
}
static void adapter_release(void* self, uint32_t frames) { winmm_release((AT_WinmmBackend*)self, frames); }

AT_Backend winmm_backend(AT_WinmmBackend* state) {
    return (AT_Backend){
        .name      = "winmm",
        .self      = state,
        .info      = &state->info,
        .available = adapter_available,
        .enumerate = adapter_enumerate,
        .open      = adapter_open,
        .start     = adapter_start,
        .stop      = adapter_stop,
        .close     = adapter_close,
        .wait      = adapter_wait,
        .wake      = adapter_wake,
        .acquire   = adapter_acquire,
        .release   = adapter_release
        // no per-thread setup: waveOut needs none
    };
}
