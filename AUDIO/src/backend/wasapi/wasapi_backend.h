// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_backend.h — raw C WASAPI render device
//
//  One of the audio backends (backend/audio_backend.h). Needs Windows Vista or
//  later; on older systems wasapi_backend().available returns false and the
//  WinMM backend takes over (backend/select/backend_select.h).
//
//  Shared mode:    always float32 stereo; Windows converts rate/channels for us
//                  (AUTOCONVERTPCM). Latency ~10–20 ms, never fails on format.
//  Exclusive mode: bypasses the Windows mixer. Probes float32 → int32 →
//                  int24-in-32 → packed int24 → int16 at the requested rate,
//                  uses the device's minimum period (~3 ms on most hardware).
//
//  Files in this folder:
//    wasapi_backend.c   the AT_Backend adapter (bool + error text instead of HRESULT)
//    wasapi_com.c       GUIDs, per-thread COM, error strings
//    wasapi_devices.c   enumeration, device names
//    wasapi_format.c    shared / exclusive format negotiation
//    wasapi_stream.c    open, start, stop, wait / acquire / release
//
//      static AT_WasapiBackend state;
//      AT_Backend backend = wasapi_backend(&state);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "backend/audio_backend.h"

// Opaque COM types — only the wasapi_*.c files include the SDK audio headers.
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;

typedef struct {
    AT_BackendInfo              info;

    // ── internals ──
    struct IMMDeviceEnumerator* enumerator;
    struct IMMDevice*           device;
    struct IAudioClient*        client;
    struct IAudioRenderClient*  render;
    HANDLE                      event;
    bool                        com_initialized;
    bool                        started;
} AT_WasapiBackend;

// Describes the backend to the rest of the program. `state` must outlive the
// returned struct's use.
AT_Backend wasapi_backend(AT_WasapiBackend* state);
