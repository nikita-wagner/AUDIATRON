// ═══════════════════════════════════════════════════════════════════════════
//  backend/audio_backend.h — what every audio device backend looks like
//
//  A backend is the DEVICE and nothing else: no thread, no DSP, no engine.
//  WASAPI (Windows Vista and later) and WinMM (every Windows since 95) both
//  implement this one contract. Everything above it — audio thread, engine,
//  tracks, layers, effects — is the same code on either, so the engine sounds
//  and behaves the same whichever one is running.
//
//      1. find a device             backend_enumerate
//      2. open a stream on it       backend_open  → b->info says what was negotiated
//      3. start / stop / close
//      4. hand out the device buffer once per period:
//             backend_wait → backend_acquire → (fill it) → backend_release
//
//  Who calls step 4 is not the backend's business — see threads/audio/.
//
//  Picking one at run time:   backend/select/backend_select.h
//  Adding one (DirectSound, ASIO, ...): implement these functions in a new
//  folder under backend/ and return them in an AT_Backend — like
//  backend/winmm/winmm_backend.c does.
//
//  Threading: open and close on the same thread. Every OTHER thread that calls
//  backend_wait / acquire / release must be bracketed by
//  backend_thread_attach / backend_thread_detach.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "io/sample_format/sample_format.h"

typedef struct {
    int      device_index;      // -1 = system default; otherwise an index from backend_enumerate
    uint32_t sample_rate;       // 0 = the backend's own choice (WASAPI: device mix rate, WinMM: 44100)
    uint32_t period_us;         // 0 = the backend's default
    uint32_t queue_us;          // audio kept queued at the device, 0 = default. WinMM: this IS its
                                // latency, and too little of it crackles. WASAPI: the device decides.
    bool     exclusive;         // bypass the system mixer where the backend can (WASAPI); ignored otherwise
} AT_BackendConfig;

typedef struct {
    // ── negotiated stream, valid after a successful open ──
    uint32_t      sample_rate;
    uint32_t      channels;             // DEVICE channels (the engine is always stereo)
    uint32_t      bytes_per_sample;     // container size
    uint32_t      valid_bits;
    AT_SampleType sample_type;
    uint32_t      buffer_frames;        // most frames one backend_acquire hands out
    double        period_ms;            // how often the device asks for data
    double        latency_ms;           // audio queued inside the backend when it is full
    bool          exclusive;
    char          device_name[128];     // UTF-8

    char          error[192];           // why the last open / start failed
    volatile long device_lost;          // set when the device vanished while streaming
    volatile long underruns;            // times the device ran out of queued audio (WinMM counts these)
} AT_BackendInfo;

typedef void (*AT_DeviceEnumFn)(uint32_t index, const char* name_utf8, bool is_default, void* user);

typedef struct {
    const char*     name;               // "wasapi", "winmm"
    void*           self;               // the backend's own state struct
    AT_BackendInfo* info;               // lives inside that state

    bool (*available)(void* self);                                      // does this backend exist on this Windows?
    bool (*enumerate)(void* self, AT_DeviceEnumFn fn, void* user);

    bool (*open) (void* self, const AT_BackendConfig* cfg);             // false: see info->error
    bool (*start)(void* self);
    void (*stop) (void* self);          // stop whoever fills the buffer BEFORE calling this
    void (*close)(void* self);

    bool (*wait)   (void* self, uint32_t timeout_ms);
    void (*wake)   (void* self);
    bool (*acquire)(void* self, uint8_t** data, uint32_t* frames);
    void (*release)(void* self, uint32_t frames);

    bool (*thread_attach)(void* self);                                  // optional (NULL = nothing to do)
    void (*thread_detach)(void* self, bool token);
} AT_Backend;

// ─── Devices ─────────────────────────────────────────────────────────────────

static inline bool backend_available(const AT_Backend* b) { return b->available(b->self); }

// Calls fn once per output device. Index N can be passed as AT_BackendConfig.device_index.
static inline bool backend_enumerate(const AT_Backend* b, AT_DeviceEnumFn fn, void* user) {
    return b->enumerate(b->self, fn, user);
}

// ─── Stream lifetime ─────────────────────────────────────────────────────────

static inline bool backend_open (AT_Backend* b, const AT_BackendConfig* cfg) { return b->open(b->self, cfg); }
static inline bool backend_start(AT_Backend* b) { return b->start(b->self); }
static inline void backend_stop (AT_Backend* b) { b->stop(b->self); }
static inline void backend_close(AT_Backend* b) { b->close(b->self); }

// ─── Per-period buffer exchange ──────────────────────────────────────────────

// Blocks until the device wants data (true) or the timeout passes (false).
// backend_wake also makes it return true — check your stop flag after waiting.
static inline bool backend_wait(AT_Backend* b, uint32_t timeout_ms) { return b->wait(b->self, timeout_ms); }

// Makes a blocked backend_wait return at once, so its thread can check a stop flag.
static inline void backend_wake(AT_Backend* b) { b->wake(b->self); }

// Hands out device memory that needs filling, in the format b->info describes.
// *frames may be 0 (nothing to do right now). false = the stream is dead; if
// the device vanished, info->device_lost is set.
static inline bool backend_acquire(AT_Backend* b, uint8_t** data, uint32_t* frames) {
    return b->acquire(b->self, data, frames);
}

// Gives the filled memory back; it is now on its way to the speakers.
static inline void backend_release(AT_Backend* b, uint32_t frames) { b->release(b->self, frames); }

// ─── Per-thread setup ────────────────────────────────────────────────────────

// Returns a token for backend_thread_detach.
static inline bool backend_thread_attach(AT_Backend* b) {
    return b->thread_attach ? b->thread_attach(b->self) : false;
}
static inline void backend_thread_detach(AT_Backend* b, bool token) {
    if (b->thread_detach) b->thread_detach(b->self, token);
}
