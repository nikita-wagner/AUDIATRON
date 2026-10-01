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

// wasapi_com.c — S_OK and *owned = true means the caller owes a CoUninitialize.
HRESULT wasapi_com_enter(bool* owned);

// wasapi_devices.c
void wasapi_read_friendly_name(IMMDevice* device, wchar_t* out, size_t cap);

// wasapi_format.c — both initialize b->client and fill in the negotiated format.
HRESULT wasapi_activate_client(AT_Backend* b);
HRESULT wasapi_init_shared    (AT_Backend* b, const AT_BackendConfig* cfg, uint32_t rate);
HRESULT wasapi_init_exclusive (AT_Backend* b, const AT_BackendConfig* cfg,
                               const WAVEFORMATEX* mix, uint32_t rate);
