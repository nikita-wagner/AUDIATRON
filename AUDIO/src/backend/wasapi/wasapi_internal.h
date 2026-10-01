// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_internal.h — shared by the wasapi_*.c files only
//
//  COM from plain C: every interface is a struct whose first member points to
//  a table of function pointers.
//
//      C++ :  client->Initialize(...)
//      C   :  client->lpVtbl->Initialize(client, ...)   // pass "this" yourself
//
//  (Defining COBJMACROS before the includes gives you IAudioClient_Initialize()
//  style macros instead — same thing, spelled differently. This backend writes
//  the lpVtbl calls out on purpose, so the mechanism stays visible.)
//
//  Every interface you receive must be Release()d exactly once.
//
//  The functions below speak HRESULT, like the API they wrap. wasapi_backend.c
//  turns that into the bool + error text of the backend contract.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

#include "backend/wasapi/wasapi_backend.h"

#define SAFE_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

#define REFTIMES_PER_MS 10000       // REFERENCE_TIME is in 100 ns units

// GUIDs — defined in wasapi_com.c (like miniaudio does) so we don't depend on
// which import library happens to export them for C.
extern const CLSID       AT_CLSID_MMDeviceEnumerator;
extern const IID         AT_IID_IMMDeviceEnumerator;
extern const IID         AT_IID_IAudioClient;
extern const IID         AT_IID_IAudioRenderClient;
extern const GUID        AT_SUBTYPE_PCM;
extern const GUID        AT_SUBTYPE_IEEE_FLOAT;
extern const PROPERTYKEY AT_PKEY_Device_FriendlyName;

// ─── wasapi_com.c ────────────────────────────────────────────────────────────

// S_OK and *owned = true means the caller owes a CoUninitialize.
HRESULT     wasapi_com_enter(bool* owned);
const char* wasapi_hr_string(HRESULT hr);

// ─── wasapi_devices.c ────────────────────────────────────────────────────────

typedef void (*AT_WasapiDeviceFn)(uint32_t index, const wchar_t* id, const wchar_t* name,
                                  bool is_default, void* user);

// Lists active render endpoints (initializes COM on the calling thread as needed).
HRESULT wasapi_enumerate_devices(AT_WasapiDeviceFn fn, void* user);
void    wasapi_read_friendly_name(IMMDevice* device, wchar_t* out, size_t cap);

// ─── wasapi_format.c — both initialize b->client and fill in b->info ─────────

HRESULT wasapi_activate_client(AT_WasapiBackend* b);
HRESULT wasapi_init_shared    (AT_WasapiBackend* b, const AT_BackendConfig* cfg, uint32_t rate);
HRESULT wasapi_init_exclusive (AT_WasapiBackend* b, const AT_BackendConfig* cfg,
                               const WAVEFORMATEX* mix, uint32_t rate);

// ─── wasapi_stream.c ─────────────────────────────────────────────────────────

// device_id = NULL opens the system default render device.
HRESULT wasapi_open (AT_WasapiBackend* b, const AT_BackendConfig* cfg, const wchar_t* device_id);
HRESULT wasapi_start(AT_WasapiBackend* b);      // pre-rolls one buffer of silence, starts the device
void    wasapi_stop (AT_WasapiBackend* b);
void    wasapi_close(AT_WasapiBackend* b);      // releases every COM object

bool    wasapi_wait   (AT_WasapiBackend* b, uint32_t timeout_ms);
void    wasapi_wake   (AT_WasapiBackend* b);
HRESULT wasapi_acquire(AT_WasapiBackend* b, uint8_t** data, uint32_t* frames);
void    wasapi_release(AT_WasapiBackend* b, uint32_t frames);
