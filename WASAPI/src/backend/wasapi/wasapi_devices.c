// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_devices.c — device enumeration and names
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/wasapi/wasapi_internal.h"

void wasapi_read_friendly_name(IMMDevice* device, wchar_t* out, size_t cap) {
    out[0] = L'\0';
    IPropertyStore* props = NULL;
    if (FAILED(device->lpVtbl->OpenPropertyStore(device, STGM_READ, &props))) return;

    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (SUCCEEDED(props->lpVtbl->GetValue(props, &AT_PKEY_Device_FriendlyName, &pv)) &&
        pv.vt == VT_LPWSTR && pv.pwszVal)
        wcsncpy_s(out, cap, pv.pwszVal, _TRUNCATE);

    PropVariantClear(&pv);
    SAFE_RELEASE(props);
}

HRESULT backend_enumerate_devices(AT_DeviceEnumFn fn, void* user) {
    bool com_owned = false;
    HRESULT hr = wasapi_com_enter(&com_owned);
    if (FAILED(hr)) return hr;

    IMMDeviceEnumerator* enumerator = NULL;
    IMMDeviceCollection* collection = NULL;
    IMMDevice*           def_device = NULL;
    LPWSTR               def_id     = NULL;

    hr = CoCreateInstance(&AT_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &AT_IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) goto done;

    if (SUCCEEDED(enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &def_device)))
        def_device->lpVtbl->GetId(def_device, &def_id);

    hr = enumerator->lpVtbl->EnumAudioEndpoints(enumerator, eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) goto done;

    UINT count = 0;
    collection->lpVtbl->GetCount(collection, &count);
    for (UINT i = 0; i < count; i++) {
        IMMDevice* device = NULL;
        LPWSTR     id     = NULL;
        if (FAILED(collection->lpVtbl->Item(collection, i, &device))) continue;

        if (SUCCEEDED(device->lpVtbl->GetId(device, &id))) {
            wchar_t name[128];
            wasapi_read_friendly_name(device, name, sizeof name / sizeof *name);
            fn(i, id, name, def_id && wcscmp(id, def_id) == 0, user);
            CoTaskMemFree(id);
        }
        SAFE_RELEASE(device);
    }

done:
    if (def_id) CoTaskMemFree(def_id);
    SAFE_RELEASE(def_device);
    SAFE_RELEASE(collection);
    SAFE_RELEASE(enumerator);
    if (com_owned) CoUninitialize();
    return hr;
}
