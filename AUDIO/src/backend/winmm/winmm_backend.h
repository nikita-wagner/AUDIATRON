// ═══════════════════════════════════════════════════════════════════════════
//  backend/winmm/winmm_backend.h — WinMM (waveOut) render device
//
//  One of the audio backends (backend/audio_backend.h), and the one that runs
//  everywhere: waveOut exists in every Windows since 95. It uses only calls
//  that Windows 98 has — ANSI functions, an event for wake-ups, no COM.
//
//  How it maps onto the backend contract:
//
//      A ring of equally sized buffers circulates between us and the device.
//      waveOut signals an event whenever it has finished playing one.
//
//          backend_wait      returns when the oldest buffer has been played
//          backend_acquire   hands that buffer out
//          backend_release   queues it again (waveOutWrite)
//
//  Format: always 16-bit stereo PCM. The engine renders float; the audio
//  thread converts once (io/sample_format), exactly as it does for WASAPI.
//
//  Two settings (AT_BackendConfig), and they do different things:
//
//      queue_us    how much audio sits at the device. This IS the latency, and
//                  the safety margin. Default 120 ms.
//      period_us   the size of one buffer = how finely notes and parameter
//                  changes are timed. Default 20 ms, so 6 buffers.
//
//  How short the queue may be depends on the machine, not on this code. On
//  Windows Vista and later, waveOut is emulated on top of WASAPI and some
//  devices take audio in gulps: a USB headset measured here needed 80 ms, an
//  onboard chip 60 ms, below that the device ran dry. info.underruns counts
//  those dropouts — if it rises, raise queue_us.
//
//  Files in this folder:
//    winmm_backend.c   the AT_Backend adapter (bool + error text instead of MMRESULT)
//    winmm_devices.c   enumeration, device names, the default device
//    winmm_stream.c    open, start, stop, wait / acquire / release
//
//      static AT_WinmmBackend state;
//      AT_Backend backend = winmm_backend(&state);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

#include "backend/audio_backend.h"

#define AT_WINMM_MAX_BUFFERS        16          // most buffers in the ring
#define AT_WINMM_POOL_FRAMES        16384       // memory all buffers share (341 ms at 48 kHz)
#define AT_WINMM_DEFAULT_RATE       44100       // what every sound card since the SB16 plays natively
#define AT_WINMM_DEFAULT_PERIOD_US  20000
#define AT_WINMM_DEFAULT_QUEUE_US   120000

typedef struct {
    AT_BackendInfo info;

    // ── internals ──
    HWAVEOUT       device;
    HANDLE         event;                       // set by waveOut when a buffer is done, and by wake
    WAVEHDR        headers[AT_WINMM_MAX_BUFFERS];
    uint32_t       buffer_count;                // buffers in the ring = queue / period
    uint32_t       next;                        // the buffer that finishes playing next
    uint32_t       written;                     // buffers queued since start (saturates)
    bool           prepared;
    bool           failed;                      // a waveOutWrite failed: the stream is dead
    volatile long  started;
    int16_t        memory[AT_WINMM_POOL_FRAMES * 2];    // stereo; carved into buffer_count buffers
} AT_WinmmBackend;

// Describes the backend to the rest of the program. `state` must outlive the
// returned struct's use.
AT_Backend winmm_backend(AT_WinmmBackend* state);

// ─── used between the files of this folder ───────────────────────────────────

// winmm_devices.c
UINT winmm_default_device(void);                                    // WAVE_MAPPER if it cannot be told
void winmm_device_name(UINT device, char* out_utf8, size_t cap);
bool winmm_enumerate_devices(AT_DeviceEnumFn fn, void* user);

// winmm_stream.c — MMRESULT like the API they wrap; winmm_backend.c adds the error text
MMRESULT winmm_open (AT_WinmmBackend* b, const AT_BackendConfig* cfg);
MMRESULT winmm_start(AT_WinmmBackend* b);
void     winmm_stop (AT_WinmmBackend* b);
void     winmm_close(AT_WinmmBackend* b);

bool winmm_wait   (AT_WinmmBackend* b, uint32_t timeout_ms);
void winmm_wake   (AT_WinmmBackend* b);
bool winmm_acquire(AT_WinmmBackend* b, uint8_t** data, uint32_t* frames);
void winmm_release(AT_WinmmBackend* b, uint32_t frames);
