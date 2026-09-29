// ═══════════════════════════════════════════════════════════════════════════
//  wasapi_backend.h — raw C WASAPI render backend (shared + exclusive)
//
//  Knows NOTHING about synths, EQs or mixers. Its whole job:
//    1. find the device (IMMDeviceEnumerator → IMMDevice)
//    2. negotiate a format and open the stream (IAudioClient)
//    3. run one high-priority, event-driven thread that, every time the
//       device signals "I need data", calls AT_RenderCallback for planar
//       float frames and converts them into the device's native format
//       (IAudioRenderClient::GetBuffer / ReleaseBuffer)
//
//  Shared mode:    always float32 stereo; Windows converts rate/channels for us
//                  (AUTOCONVERTPCM). Latency ~10–20 ms, never fails on format.
//  Exclusive mode: bypasses the Windows mixer. Probes float32 → int32 →
//                  int24-in-32 → packed int24 → int16 at the requested rate,
//                  uses the device's minimum period (~3 ms on most hardware).
//
//  COM rule: backend_open and backend_close must run on the same thread.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "at_common.h"

// Opaque COM types — only wasapi_backend.c includes the SDK audio headers.
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;

typedef enum {
    AT_SHARE_SHARED,
    AT_SHARE_EXCLUSIVE
} AT_ShareMode;

typedef enum {
    AT_SAMPLE_F32,
    AT_SAMPLE_S16,
    AT_SAMPLE_S24,          // packed, 3 bytes
    AT_SAMPLE_S32           // 4 bytes (also used for 24-bit-in-32 containers)
} AT_SampleType;

typedef struct {
    AT_ShareMode      share_mode;
    const wchar_t*    device_id;        // NULL = system default render device
    uint32_t          sample_rate;      // 0 = device mix rate
    uint32_t          period_us;        // 0 = device minimum (exclusive) / default (shared)
    AT_RenderCallback callback;
    void*             user;
} AT_BackendConfig;

typedef struct {
    // ── negotiated stream info (valid after backend_open) ──
    AT_ShareMode    share_mode;
    uint32_t        sample_rate;
    uint32_t        channels;           // DEVICE channels (engine is always stereo)
    uint32_t        bytes_per_sample;   // container size
    uint32_t        valid_bits;
    AT_SampleType   sample_type;
    uint32_t        buffer_frames;      // WASAPI endpoint buffer size
    double          period_ms;
    wchar_t         device_name[128];

    volatile LONG   device_lost;        // set by the audio thread (unplugged, format change...)
    volatile LONG   callback_overruns;  // blocks where rendering took longer than one period

    // ── internals ──
    struct IMMDeviceEnumerator* enumerator;
    struct IMMDevice*           device;
    struct IAudioClient*        client;
    struct IAudioRenderClient*  render;
    HANDLE                      event;
    HANDLE                      thread;
    volatile LONG               stop;
    bool                        com_initialized;
    AT_RenderCallback           callback;
    void*                       user;
    uint64_t                    frame_clock;
    float                       scratch[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];
} AT_Backend;

typedef void (*AT_DeviceEnumFn)(uint32_t index, const wchar_t* id, const wchar_t* name,
                                bool is_default, void* user);

// Lists active render endpoints (initializes COM on the calling thread as needed).
HRESULT backend_enumerate_devices(AT_DeviceEnumFn fn, void* user);

HRESULT backend_open (AT_Backend* b, const AT_BackendConfig* cfg);
HRESULT backend_start(AT_Backend* b);   // spawns the audio thread
void    backend_stop (AT_Backend* b);   // joins the audio thread
void    backend_close(AT_Backend* b);   // releases every COM object

const char* backend_sample_type_name(AT_SampleType t);
const char* backend_hr_string(HRESULT hr);
