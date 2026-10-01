// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_stream.c — stream lifetime + per-period buffer exchange
//
//  The goto-fail pattern in wasapi_open keeps the "Release everything exactly
//  once" rule manageable: every failure path ends in wasapi_close.
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/wasapi/wasapi_internal.h"

// ─── Open / close ────────────────────────────────────────────────────────────

HRESULT wasapi_open(AT_WasapiBackend* b, const AT_BackendConfig* cfg, const wchar_t* device_id) {
    memset(b, 0, sizeof *b);
    b->info.exclusive = cfg->exclusive;

    WAVEFORMATEX* mix = NULL;
    HRESULT hr = wasapi_com_enter(&b->com_initialized);
    if (FAILED(hr)) return hr;

    hr = CoCreateInstance(&AT_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &AT_IID_IMMDeviceEnumerator, (void**)&b->enumerator);
    if (FAILED(hr)) goto fail;

    if (device_id)
        hr = b->enumerator->lpVtbl->GetDevice(b->enumerator, device_id, &b->device);
    else
        hr = b->enumerator->lpVtbl->GetDefaultAudioEndpoint(b->enumerator, eRender, eConsole, &b->device);
    if (FAILED(hr)) goto fail;

    wchar_t name[128];
    wasapi_read_friendly_name(b->device, name, sizeof name / sizeof *name);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, b->info.device_name, sizeof b->info.device_name, NULL, NULL);
    b->info.device_name[sizeof b->info.device_name - 1] = '\0';

    hr = wasapi_activate_client(b);
    if (FAILED(hr)) goto fail;

    // The mix format is what the Windows mixer runs at (shared mode). It is
    // only a starting point for exclusive mode, which talks to the hardware.
    hr = b->client->lpVtbl->GetMixFormat(b->client, &mix);
    if (FAILED(hr)) goto fail;

    uint32_t rate = cfg->sample_rate ? cfg->sample_rate : mix->nSamplesPerSec;
    hr = cfg->exclusive ? wasapi_init_exclusive(b, cfg, mix, rate)
                        : wasapi_init_shared(b, cfg, rate);
    CoTaskMemFree(mix);
    mix = NULL;
    if (FAILED(hr)) goto fail;

    // Auto-reset event: the device signals it once per period.
    b->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!b->event) { hr = HRESULT_FROM_WIN32(GetLastError()); goto fail; }

    hr = b->client->lpVtbl->SetEventHandle(b->client, b->event);
    if (FAILED(hr)) goto fail;

    UINT32 frames = 0;
    hr = b->client->lpVtbl->GetBufferSize(b->client, &frames);
    if (FAILED(hr)) goto fail;
    b->info.buffer_frames = frames;
    b->info.latency_ms    = 1000.0 * frames / b->info.sample_rate;

    hr = b->client->lpVtbl->GetService(b->client, &AT_IID_IAudioRenderClient, (void**)&b->render);
    if (FAILED(hr)) goto fail;

    return S_OK;

fail:
    if (mix) CoTaskMemFree(mix);
    wasapi_close(b);
    return hr;
}

void wasapi_close(AT_WasapiBackend* b) {
    wasapi_stop(b);
    SAFE_RELEASE(b->render);
    SAFE_RELEASE(b->client);
    SAFE_RELEASE(b->device);
    SAFE_RELEASE(b->enumerator);
    if (b->event) { CloseHandle(b->event); b->event = NULL; }
    if (b->com_initialized) { CoUninitialize(); b->com_initialized = false; }
}

// ─── Start / stop ────────────────────────────────────────────────────────────

HRESULT wasapi_start(AT_WasapiBackend* b) {
    if (!b->client || !b->render) return AUDCLNT_E_NOT_INITIALIZED;
    if (b->started) return S_OK;

    // Pre-roll one buffer of silence so the very first period can't underrun.
    BYTE* data = NULL;
    HRESULT hr = b->render->lpVtbl->GetBuffer(b->render, b->info.buffer_frames, &data);
    if (SUCCEEDED(hr))
        b->render->lpVtbl->ReleaseBuffer(b->render, b->info.buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);

    hr = b->client->lpVtbl->Start(b->client);
    b->started = SUCCEEDED(hr);
    return hr;
}

void wasapi_stop(AT_WasapiBackend* b) {
    if (!b->started) return;
    b->client->lpVtbl->Stop(b->client);
    b->client->lpVtbl->Reset(b->client);
    b->started = false;
}

// ─── Per-period buffer exchange ──────────────────────────────────────────────

bool wasapi_wait(AT_WasapiBackend* b, uint32_t timeout_ms) {
    return WaitForSingleObject(b->event, timeout_ms) == WAIT_OBJECT_0;
}

void wasapi_wake(AT_WasapiBackend* b) {
    if (b->event) SetEvent(b->event);
}

static HRESULT stream_failed(AT_WasapiBackend* b, HRESULT hr) {
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) InterlockedExchange(&b->info.device_lost, 1);
    return hr;
}

HRESULT wasapi_acquire(AT_WasapiBackend* b, uint8_t** data, uint32_t* frames) {
    *data   = NULL;
    *frames = 0;

    // Exclusive: the whole buffer is ours every period.
    // Shared:    only the part the Windows mixer has already consumed.
    UINT32 want = b->info.buffer_frames;
    if (!b->info.exclusive) {
        UINT32 padding = 0;
        HRESULT hr = b->client->lpVtbl->GetCurrentPadding(b->client, &padding);
        if (FAILED(hr)) return stream_failed(b, hr);
        want -= padding;
    }
    if (want == 0) return S_OK;

    BYTE*   bytes = NULL;
    HRESULT hr    = b->render->lpVtbl->GetBuffer(b->render, want, &bytes);
    if (FAILED(hr)) return stream_failed(b, hr);

    *data   = bytes;
    *frames = want;
    return S_OK;
}

void wasapi_release(AT_WasapiBackend* b, uint32_t frames) {
    b->render->lpVtbl->ReleaseBuffer(b->render, frames, 0);
}
