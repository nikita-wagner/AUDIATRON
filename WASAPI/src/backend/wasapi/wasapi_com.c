// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_com.c — COM plumbing: GUIDs, per-thread init, errors
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/wasapi/wasapi_internal.h"

// ─── GUIDs ───────────────────────────────────────────────────────────────────

const CLSID AT_CLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
const IID AT_IID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
const IID AT_IID_IAudioClient =
    { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
const IID AT_IID_IAudioRenderClient =
    { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
const GUID AT_SUBTYPE_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
const GUID AT_SUBTYPE_IEEE_FLOAT =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
const PROPERTYKEY AT_PKEY_Device_FriendlyName =
    { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

// ─── Per-thread COM ──────────────────────────────────────────────────────────

// S_OK / S_FALSE → we own a CoUninitialize. RPC_E_CHANGED_MODE → the thread is
// already STA, which works for WASAPI too; just don't uninitialize it.
HRESULT wasapi_com_enter(bool* owned) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    *owned = SUCCEEDED(hr);
    return hr == RPC_E_CHANGED_MODE ? S_OK : hr;
}

bool backend_thread_attach(void) {
    bool owned = false;
    wasapi_com_enter(&owned);
    return owned;
}

void backend_thread_detach(bool token) {
    if (token) CoUninitialize();
}

// ─── Error strings ───────────────────────────────────────────────────────────

const char* backend_hr_string(HRESULT hr) {
    switch (hr) {
    case S_OK:                                  return "S_OK";
    case E_POINTER:                             return "E_POINTER";
    case E_INVALIDARG:                          return "E_INVALIDARG";
    case E_OUTOFMEMORY:                         return "E_OUTOFMEMORY";
    case (HRESULT)0x80070490:                   return "E_NOTFOUND (no such device)";
    case AUDCLNT_E_UNSUPPORTED_FORMAT:          return "AUDCLNT_E_UNSUPPORTED_FORMAT";
    case AUDCLNT_E_DEVICE_IN_USE:               return "AUDCLNT_E_DEVICE_IN_USE (another app owns it exclusively)";
    case AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED:  return "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED (disabled in Sound control panel)";
    case AUDCLNT_E_DEVICE_INVALIDATED:          return "AUDCLNT_E_DEVICE_INVALIDATED (device removed/changed)";
    case AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED:     return "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED";
    case AUDCLNT_E_BUFFER_SIZE_ERROR:           return "AUDCLNT_E_BUFFER_SIZE_ERROR";
    case AUDCLNT_E_INVALID_DEVICE_PERIOD:       return "AUDCLNT_E_INVALID_DEVICE_PERIOD";
    case AUDCLNT_E_ENDPOINT_CREATE_FAILED:      return "AUDCLNT_E_ENDPOINT_CREATE_FAILED";
    case AUDCLNT_E_SERVICE_NOT_RUNNING:         return "AUDCLNT_E_SERVICE_NOT_RUNNING";
    case AUDCLNT_E_CPUUSAGE_EXCEEDED:           return "AUDCLNT_E_CPUUSAGE_EXCEEDED";
    case AUDCLNT_E_NOT_INITIALIZED:             return "AUDCLNT_E_NOT_INITIALIZED";
    case AUDCLNT_E_ALREADY_INITIALIZED:         return "AUDCLNT_E_ALREADY_INITIALIZED";
    default:                                    return "unknown HRESULT";
    }
}
