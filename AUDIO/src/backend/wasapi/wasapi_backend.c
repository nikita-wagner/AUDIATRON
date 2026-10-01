// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_backend.c — WASAPI as an AT_Backend
//
//  The only file in this folder that deals with the backend contract
//  (backend/audio_backend.h). It turns HRESULTs into bool + error text and
//  device indices into WASAPI's device id strings.
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/wasapi/wasapi_internal.h"

#include <stdio.h>

static bool fail(AT_WasapiBackend* b, const char* what, HRESULT hr) {
    snprintf(b->info.error, sizeof b->info.error, "%s failed: 0x%08lX %s",
             what, (unsigned long)hr, wasapi_hr_string(hr));
    return false;
}

// ─── Devices ─────────────────────────────────────────────────────────────────

// True on Windows Vista and later: the device enumerator can be created.
static bool adapter_available(void* self) {
    (void)self;
    return SUCCEEDED(wasapi_enumerate_devices(NULL, NULL));
}

typedef struct { AT_DeviceEnumFn fn; void* user; } EnumForward;

static void forward_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)id;
    const EnumForward* f = (const EnumForward*)user;

    char utf8[128] = "";
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, NULL, NULL);
    utf8[sizeof utf8 - 1] = '\0';
    f->fn(index, utf8, is_default, f->user);
}

static bool adapter_enumerate(void* self, AT_DeviceEnumFn fn, void* user) {
    (void)self;
    EnumForward forward = { fn, user };
    return SUCCEEDED(wasapi_enumerate_devices(forward_device, &forward));
}

// device index → WASAPI device id
typedef struct { int wanted; wchar_t id[512]; bool found; } DevicePick;

static void pick_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)name; (void)is_default;
    DevicePick* pick = (DevicePick*)user;
    if ((int)index == pick->wanted) {
        wcsncpy_s(pick->id, sizeof pick->id / sizeof *pick->id, id, _TRUNCATE);
        pick->found = true;
    }
}

// ─── Stream ──────────────────────────────────────────────────────────────────

static bool adapter_open(void* self, const AT_BackendConfig* cfg) {
    AT_WasapiBackend* b = (AT_WasapiBackend*)self;

    DevicePick pick = { .wanted = cfg->device_index };
    if (cfg->device_index >= 0) {
        wasapi_enumerate_devices(pick_device, &pick);
        if (!pick.found) {
            memset(b, 0, sizeof *b);
            snprintf(b->info.error, sizeof b->info.error, "no WASAPI device with index %d", cfg->device_index);
            return false;
        }
    }

    HRESULT hr = wasapi_open(b, cfg, cfg->device_index >= 0 ? pick.id : NULL);
    return SUCCEEDED(hr) ? true : fail(b, "opening the WASAPI device", hr);
}

static bool adapter_start(void* self) {
    AT_WasapiBackend* b = (AT_WasapiBackend*)self;
    HRESULT hr = wasapi_start(b);
    return SUCCEEDED(hr) ? true : fail(b, "starting the WASAPI stream", hr);
}

static void adapter_stop (void* self) { wasapi_stop((AT_WasapiBackend*)self); }
static void adapter_close(void* self) { wasapi_close((AT_WasapiBackend*)self); }

static bool adapter_wait(void* self, uint32_t timeout_ms) { return wasapi_wait((AT_WasapiBackend*)self, timeout_ms); }
static void adapter_wake(void* self)                      { wasapi_wake((AT_WasapiBackend*)self); }

static bool adapter_acquire(void* self, uint8_t** data, uint32_t* frames) {
    return SUCCEEDED(wasapi_acquire((AT_WasapiBackend*)self, data, frames));
}
static void adapter_release(void* self, uint32_t frames) { wasapi_release((AT_WasapiBackend*)self, frames); }

// ─── Per-thread COM ──────────────────────────────────────────────────────────

static bool adapter_thread_attach(void* self) {
    (void)self;
    bool owned = false;
    wasapi_com_enter(&owned);
    return owned;
}

static void adapter_thread_detach(void* self, bool token) {
    (void)self;
    if (token) CoUninitialize();
}

AT_Backend wasapi_backend(AT_WasapiBackend* state) {
    return (AT_Backend){
        .name          = "wasapi",
        .self          = state,
        .info          = &state->info,
        .available     = adapter_available,
        .enumerate     = adapter_enumerate,
        .open          = adapter_open,
        .start         = adapter_start,
        .stop          = adapter_stop,
        .close         = adapter_close,
        .wait          = adapter_wait,
        .wake          = adapter_wake,
        .acquire       = adapter_acquire,
        .release       = adapter_release,
        .thread_attach = adapter_thread_attach,
        .thread_detach = adapter_thread_detach
    };
}
