// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_backend.h — raw C WASAPI render device
//
//  The device and nothing else: no thread, no DSP, no engine. Its whole job:
//    1. find the device                    (wasapi_devices.c)
//    2. negotiate a format                 (wasapi_format.c)
//    3. open / start / stop the stream     (wasapi_stream.c)
//    4. hand out the device buffer once per period:
//           backend_wait → backend_acquire → (fill it) → backend_release
//
//  Who calls step 4 is not this module's business — see threads/audio/.
//
//  Shared mode:    always float32 stereo; Windows converts rate/channels for us
//                  (AUTOCONVERTPCM). Latency ~10–20 ms, never fails on format.
//  Exclusive mode: bypasses the Windows mixer. Probes float32 → int32 →
//                  int24-in-32 → packed int24 → int16 at the requested rate,
//                  uses the device's minimum period (~3 ms on most hardware).
//
//  COM rule: backend_open and backend_close must run on the same thread, and
//  every other thread that calls backend_* must be bracketed by
//  backend_thread_attach / backend_thread_detach.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#include "io/sample_format/sample_format.h"

// Opaque COM types — only the wasapi_*.c files include the SDK audio headers.
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;

typedef enum {
    AT_SHARE_SHARED,
    AT_SHARE_EXCLUSIVE
} AT_ShareMode;

typedef struct {
    AT_ShareMode   share_mode;
    const wchar_t* device_id;           // NULL = system default render device
    uint32_t       sample_rate;         // 0 = device mix rate
    uint32_t       period_us;           // 0 = device minimum (exclusive) / default (shared)
} AT_BackendConfig;

typedef struct {
    // ── negotiated stream info (valid after backend_open) ──
    AT_ShareMode    share_mode;
    uint32_t        sample_rate;
    uint32_t        channels;           // DEVICE channels (the engine is always stereo)
    uint32_t        bytes_per_sample;   // container size
    uint32_t        valid_bits;
    AT_SampleType   sample_type;
    uint32_t        buffer_frames;      // WASAPI endpoint buffer size
    double          period_ms;
    wchar_t         device_name[128];

    volatile LONG   device_lost;        // set when the device was unplugged / reconfigured

    // ── internals ──
    struct IMMDeviceEnumerator* enumerator;
    struct IMMDevice*           device;
    struct IAudioClient*        client;
    struct IAudioRenderClient*  render;
    HANDLE                      event;
    bool                        com_initialized;
    bool                        started;
} AT_Backend;

// ─── Devices (wasapi_devices.c) ──────────────────────────────────────────────

typedef void (*AT_DeviceEnumFn)(uint32_t index, const wchar_t* id, const wchar_t* name,
                                bool is_default, void* user);

// Lists active render endpoints (initializes COM on the calling thread as needed).
HRESULT backend_enumerate_devices(AT_DeviceEnumFn fn, void* user);

// ─── Stream lifetime (wasapi_stream.c) ───────────────────────────────────────

HRESULT backend_open (AT_Backend* b, const AT_BackendConfig* cfg);
HRESULT backend_start(AT_Backend* b);   // pre-rolls one buffer of silence, starts the device
void    backend_stop (AT_Backend* b);   // stop whoever fills the buffer BEFORE calling this
void    backend_close(AT_Backend* b);   // releases every COM object

// ─── Per-period buffer exchange (wasapi_stream.c) ────────────────────────────

// Blocks until the device wants data (true) or the timeout passes (false).
// backend_wake also makes it return true — check your stop flag after waiting.
bool backend_wait(AT_Backend* b, uint32_t timeout_ms);

// Hands out the part of the device buffer that needs filling. *frames may be 0
// (nothing to do this period). A failure means the stream is dead; if the
// device vanished, device_lost is set.
HRESULT backend_acquire(AT_Backend* b, uint8_t** data, uint32_t* frames);
void    backend_release(AT_Backend* b, uint32_t frames);

// Makes a blocked backend_wait return at once, so its thread can check a stop flag.
void backend_wake(AT_Backend* b);

// ─── COM per thread + strings (wasapi_com.c) ─────────────────────────────────

bool backend_thread_attach(void);                   // returns a token for detach
void backend_thread_detach(bool token);

const char* backend_hr_string(HRESULT hr);
