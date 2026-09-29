// ═══════════════════════════════════════════════════════════════════════════
//  wasapi_backend.c — COM from plain C
//
//  Every COM interface in C is a struct whose first member is a pointer to a
//  table of function pointers:
//
//      C++ :  client->Initialize(...)
//      C   :  client->lpVtbl->Initialize(client, ...)   // pass "this" yourself
//
//  (Defining COBJMACROS before the includes gives you IAudioClient_Initialize()
//  style macros instead — same thing, spelled differently. This file writes the
//  lpVtbl calls out on purpose, so the mechanism stays visible.)
//
//  Every interface you receive must be Release()d exactly once. The goto-fail
//  pattern below keeps that manageable.
// ═══════════════════════════════════════════════════════════════════════════

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <avrt.h>
#include <stdio.h>

#include "wasapi_backend.h"

// ─── GUIDs ───────────────────────────────────────────────────────────────────
// Defined locally (like miniaudio does) so we don't depend on which import
// library happens to export them for C.

static const CLSID AT_CLSID_MMDeviceEnumerator =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const IID AT_IID_IMMDeviceEnumerator =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const IID AT_IID_IAudioClient =
    { 0x1CB9AD4C, 0xDBFA, 0x4C32, { 0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2 } };
static const IID AT_IID_IAudioRenderClient =
    { 0xF294ACFC, 0x3146, 0x4483, { 0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2 } };
static const GUID AT_SUBTYPE_PCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID AT_SUBTYPE_IEEE_FLOAT =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const PROPERTYKEY AT_PKEY_Device_FriendlyName =
    { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

#define SAFE_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

#define REFTIMES_PER_MS 10000       // REFERENCE_TIME is in 100 ns units

// ─── Strings ─────────────────────────────────────────────────────────────────

const char* backend_sample_type_name(AT_SampleType t) {
    switch (t) {
    case AT_SAMPLE_F32: return "float32";
    case AT_SAMPLE_S16: return "int16";
    case AT_SAMPLE_S24: return "int24 (packed)";
    case AT_SAMPLE_S32: return "int32";
    default:            return "?";
    }
}

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

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void read_friendly_name(IMMDevice* device, wchar_t* out, size_t cap) {
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

// S_OK / S_FALSE → we own a CoUninitialize. RPC_E_CHANGED_MODE → the thread is
// already STA, which works for WASAPI too; just don't uninitialize it.
static HRESULT com_enter(bool* owned) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    *owned = SUCCEEDED(hr);
    return hr == RPC_E_CHANGED_MODE ? S_OK : hr;
}

static void make_format(WAVEFORMATEXTENSIBLE* f, AT_SampleType type, uint32_t bytes,
                        uint32_t valid_bits, uint32_t rate, uint32_t channels, DWORD mask) {
    memset(f, 0, sizeof *f);
    f->Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    f->Format.nChannels       = (WORD)channels;
    f->Format.nSamplesPerSec  = rate;
    f->Format.wBitsPerSample  = (WORD)(bytes * 8);
    f->Format.nBlockAlign     = (WORD)(channels * bytes);
    f->Format.nAvgBytesPerSec = rate * f->Format.nBlockAlign;
    f->Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    f->Samples.wValidBitsPerSample = (WORD)valid_bits;
    f->dwChannelMask          = mask;
    f->SubFormat              = type == AT_SAMPLE_F32 ? AT_SUBTYPE_IEEE_FLOAT : AT_SUBTYPE_PCM;
}

static void store_format(AT_Backend* b, AT_SampleType type, const WAVEFORMATEXTENSIBLE* f) {
    b->sample_type      = type;
    b->sample_rate      = f->Format.nSamplesPerSec;
    b->channels         = f->Format.nChannels;
    b->bytes_per_sample = f->Format.wBitsPerSample / 8;
    b->valid_bits       = f->Samples.wValidBitsPerSample;
}

static HRESULT activate_client(AT_Backend* b) {
    return b->device->lpVtbl->Activate(b->device, &AT_IID_IAudioClient, CLSCTX_ALL,
                                       NULL, (void**)&b->client);
}

// ─── Shared mode ─────────────────────────────────────────────────────────────

static HRESULT init_shared(AT_Backend* b, const AT_BackendConfig* cfg, uint32_t rate) {
    WAVEFORMATEXTENSIBLE fmt;
    make_format(&fmt, AT_SAMPLE_F32, 4, 32, rate, 2, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);

    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM          // let Windows resample/remap
                | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    REFERENCE_TIME duration = (REFERENCE_TIME)cfg->period_us * 10;   // 0 = engine default
    HRESULT hr = b->client->lpVtbl->Initialize(b->client, AUDCLNT_SHAREMODE_SHARED, flags,
                                               duration, 0, (WAVEFORMATEX*)&fmt, NULL);
    if (FAILED(hr)) return hr;

    REFERENCE_TIME def_period = 0, min_period = 0;
    b->client->lpVtbl->GetDevicePeriod(b->client, &def_period, &min_period);
    b->period_ms = (double)def_period / REFTIMES_PER_MS;
    store_format(b, AT_SAMPLE_F32, &fmt);
    return S_OK;
}

// ─── Exclusive mode ──────────────────────────────────────────────────────────

typedef struct { AT_SampleType type; uint32_t bytes, valid_bits; } FormatCandidate;

static const FormatCandidate k_exclusive_candidates[] = {
    { AT_SAMPLE_F32, 4, 32 },
    { AT_SAMPLE_S32, 4, 32 },
    { AT_SAMPLE_S32, 4, 24 },       // 24-bit in 32-bit container (very common on USB DACs)
    { AT_SAMPLE_S24, 3, 24 },
    { AT_SAMPLE_S16, 2, 16 },
};

static HRESULT init_exclusive(AT_Backend* b, const AT_BackendConfig* cfg,
                              const WAVEFORMATEX* mix, uint32_t rate) {
    DWORD mix_mask = mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                   ? ((const WAVEFORMATEXTENSIBLE*)mix)->dwChannelMask : 0;

    // Stereo first; fall back to the device's native channel count (e.g. 8-ch
    // interfaces that refuse stereo). Extra channels get silence.
    uint32_t channel_options[2] = { 2, mix->nChannels };
    int      option_count       = mix->nChannels != 2 ? 2 : 1;

    WAVEFORMATEXTENSIBLE fmt;
    const FormatCandidate* chosen = NULL;
    for (int o = 0; o < option_count && !chosen; o++) {
        uint32_t ch   = channel_options[o];
        DWORD    mask = ch == 2 ? (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT) : mix_mask;
        for (size_t k = 0; k < sizeof k_exclusive_candidates / sizeof *k_exclusive_candidates; k++) {
            const FormatCandidate* c = &k_exclusive_candidates[k];
            make_format(&fmt, c->type, c->bytes, c->valid_bits, rate, ch, mask);
            // Exclusive mode: no "closest match" — the format either works or not.
            if (b->client->lpVtbl->IsFormatSupported(b->client, AUDCLNT_SHAREMODE_EXCLUSIVE,
                                                     (WAVEFORMATEX*)&fmt, NULL) == S_OK) {
                chosen = c;
                break;
            }
        }
    }
    if (!chosen) return AUDCLNT_E_UNSUPPORTED_FORMAT;

    REFERENCE_TIME def_period = 0, min_period = 0;
    HRESULT hr = b->client->lpVtbl->GetDevicePeriod(b->client, &def_period, &min_period);
    if (FAILED(hr)) return hr;

    REFERENCE_TIME period = cfg->period_us ? (REFERENCE_TIME)cfg->period_us * 10 : min_period;
    if (period < min_period) period = min_period;

    // In exclusive event mode, buffer duration MUST equal the periodicity.
    hr = b->client->lpVtbl->Initialize(b->client, AUDCLNT_SHAREMODE_EXCLUSIVE,
                                       AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                       period, period, (WAVEFORMATEX*)&fmt, NULL);

    // The documented dance: the device wants a frame count aligned to its own
    // granularity. Ask for the aligned size, recreate the client, retry once.
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        UINT32 aligned_frames = 0;
        b->client->lpVtbl->GetBufferSize(b->client, &aligned_frames);
        SAFE_RELEASE(b->client);

        period = (REFERENCE_TIME)(10000.0 * 1000.0 * aligned_frames / rate + 0.5);
        hr = activate_client(b);
        if (FAILED(hr)) return hr;
        hr = b->client->lpVtbl->Initialize(b->client, AUDCLNT_SHAREMODE_EXCLUSIVE,
                                           AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                           period, period, (WAVEFORMATEX*)&fmt, NULL);
    }
    if (FAILED(hr)) return hr;

    b->period_ms = (double)period / REFTIMES_PER_MS;
    store_format(b, chosen->type, &fmt);
    return S_OK;
}

// ─── Open / close ────────────────────────────────────────────────────────────

HRESULT backend_open(AT_Backend* b, const AT_BackendConfig* cfg) {
    memset(b, 0, sizeof *b);
    b->share_mode = cfg->share_mode;
    b->callback   = cfg->callback;
    b->user       = cfg->user;

    WAVEFORMATEX* mix = NULL;
    HRESULT hr = com_enter(&b->com_initialized);
    if (FAILED(hr)) return hr;

    hr = CoCreateInstance(&AT_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &AT_IID_IMMDeviceEnumerator, (void**)&b->enumerator);
    if (FAILED(hr)) goto fail;

    if (cfg->device_id)
        hr = b->enumerator->lpVtbl->GetDevice(b->enumerator, cfg->device_id, &b->device);
    else
        hr = b->enumerator->lpVtbl->GetDefaultAudioEndpoint(b->enumerator, eRender, eConsole, &b->device);
    if (FAILED(hr)) goto fail;

    read_friendly_name(b->device, b->device_name, sizeof b->device_name / sizeof *b->device_name);

    hr = activate_client(b);
    if (FAILED(hr)) goto fail;

    // The mix format is what the Windows mixer runs at (shared mode). It is
    // only a starting point for exclusive mode, which talks to the hardware.
    hr = b->client->lpVtbl->GetMixFormat(b->client, &mix);
    if (FAILED(hr)) goto fail;

    uint32_t rate = cfg->sample_rate ? cfg->sample_rate : mix->nSamplesPerSec;
    hr = cfg->share_mode == AT_SHARE_EXCLUSIVE ? init_exclusive(b, cfg, mix, rate)
                                               : init_shared(b, cfg, rate);
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
    b->buffer_frames = frames;

    hr = b->client->lpVtbl->GetService(b->client, &AT_IID_IAudioRenderClient, (void**)&b->render);
    if (FAILED(hr)) goto fail;

    return S_OK;

fail:
    if (mix) CoTaskMemFree(mix);
    backend_close(b);
    return hr;
}

void backend_close(AT_Backend* b) {
    backend_stop(b);
    SAFE_RELEASE(b->render);
    SAFE_RELEASE(b->client);
    SAFE_RELEASE(b->device);
    SAFE_RELEASE(b->enumerator);
    if (b->event) { CloseHandle(b->event); b->event = NULL; }
    if (b->com_initialized) { CoUninitialize(); b->com_initialized = false; }
}

// ─── Format conversion: planar float → interleaved device format ────────────

static inline float device_sample(const AT_Buffer* src, uint32_t dev_channels, uint32_t c, uint32_t i) {
    float x;
    if (dev_channels == 1)       x = 0.5f * (src->ch[0][i] + src->ch[1][i]);   // mono downmix
    else if (c < src->channels)  x = src->ch[c][i];
    else                         x = 0.0f;                                      // extra device channels
    return at_clampf(x, -1.0f, 1.0f);
}

static void write_device(const AT_Backend* b, BYTE* dst, const AT_Buffer* src) {
    const uint32_t dch = b->channels;
    const uint32_t n   = src->frames;

    switch (b->sample_type) {
    case AT_SAMPLE_F32: {
        float* out = (float*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dch; c++)
                *out++ = device_sample(src, dch, c, i);
        break;
    }
    case AT_SAMPLE_S16: {
        int16_t* out = (int16_t*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dch; c++)
                *out++ = (int16_t)lrintf(device_sample(src, dch, c, i) * 32767.0f);
        break;
    }
    case AT_SAMPLE_S24: {
        BYTE* out = dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dch; c++) {
                int32_t v = (int32_t)lrintf(device_sample(src, dch, c, i) * 8388607.0f);
                *out++ = (BYTE)(v);
                *out++ = (BYTE)(v >> 8);
                *out++ = (BYTE)(v >> 16);
            }
        break;
    }
    case AT_SAMPLE_S32: {
        // Full-scale 32-bit; a 24-in-32 device simply ignores the low byte.
        // double, because 1.0f * 2147483647.0f rounds up to 2^31 and overflows.
        int32_t* out = (int32_t*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dch; c++)
                *out++ = (int32_t)((double)device_sample(src, dch, c, i) * 2147483647.0);
        break;
    }
    }
}

// Fills `frames` device frames, calling the engine in chunks it can handle.
static void render_into(AT_Backend* b, BYTE* dst, uint32_t frames) {
    const uint32_t stride = b->channels * b->bytes_per_sample;

    for (uint32_t done = 0; done < frames; ) {
        uint32_t n = frames - done;
        if (n > AT_MAX_BLOCK_FRAMES) n = AT_MAX_BLOCK_FRAMES;

        AT_Buffer buf = {
            .channels    = AT_MAX_CHANNELS,
            .frames      = n,
            .sample_rate = b->sample_rate,
            .frame_clock = b->frame_clock
        };
        for (uint32_t c = 0; c < AT_MAX_CHANNELS; c++) buf.ch[c] = b->scratch[c];

        if (b->callback) b->callback(&buf, b->user);
        else             at_buffer_clear(&buf);

        write_device(b, dst + (size_t)done * stride, &buf);
        b->frame_clock += n;
        done += n;
    }
}

// ─── Audio thread ────────────────────────────────────────────────────────────

static DWORD WINAPI audio_thread_proc(LPVOID param) {
    AT_Backend* b = (AT_Backend*)param;

    bool com_owned = false;
    com_enter(&com_owned);

    // MMCSS: the Windows scheduler class for pro audio. Much stronger than
    // SetThreadPriority — this is what DAWs use.
    DWORD  task_index = 0;
    HANDLE mmcss      = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_index);
    if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);

    at_enable_flush_to_zero();

    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    const double period_ticks = b->period_ms * (double)qpf.QuadPart / 1000.0;

    while (!b->stop) {
        DWORD wait = WaitForSingleObject(b->event, 2000);
        if (b->stop) break;
        if (wait != WAIT_OBJECT_0) continue;       // timeout: device stalled, keep waiting

        // Exclusive: the whole buffer is ours every period.
        // Shared:    only the part the Windows mixer has already consumed.
        UINT32 frames = b->buffer_frames;
        if (b->share_mode == AT_SHARE_SHARED) {
            UINT32 padding = 0;
            HRESULT hr = b->client->lpVtbl->GetCurrentPadding(b->client, &padding);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) InterlockedExchange(&b->device_lost, 1);
                break;
            }
            frames -= padding;
        }
        if (frames == 0) continue;

        BYTE*   data = NULL;
        HRESULT hr   = b->render->lpVtbl->GetBuffer(b->render, frames, &data);
        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) InterlockedExchange(&b->device_lost, 1);
            break;
        }

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        render_into(b, data, frames);
        QueryPerformanceCounter(&t1);
        if ((double)(t1.QuadPart - t0.QuadPart) > period_ticks)
            InterlockedIncrement(&b->callback_overruns);

        b->render->lpVtbl->ReleaseBuffer(b->render, frames, 0);
    }

    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (com_owned) CoUninitialize();
    return 0;
}

// ─── Start / stop ────────────────────────────────────────────────────────────

HRESULT backend_start(AT_Backend* b) {
    if (!b->client || b->thread) return AUDCLNT_E_NOT_INITIALIZED;

    // Pre-roll one buffer of silence so the very first period can't underrun.
    BYTE* data = NULL;
    HRESULT hr = b->render->lpVtbl->GetBuffer(b->render, b->buffer_frames, &data);
    if (SUCCEEDED(hr))
        b->render->lpVtbl->ReleaseBuffer(b->render, b->buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);

    b->stop   = 0;
    b->thread = CreateThread(NULL, 0, audio_thread_proc, b, 0, NULL);
    if (!b->thread) return HRESULT_FROM_WIN32(GetLastError());

    hr = b->client->lpVtbl->Start(b->client);
    if (FAILED(hr)) backend_stop(b);
    return hr;
}

void backend_stop(AT_Backend* b) {
    if (!b->thread) return;

    InterlockedExchange(&b->stop, 1);
    SetEvent(b->event);                         // wake the thread if it's waiting
    WaitForSingleObject(b->thread, INFINITE);
    CloseHandle(b->thread);
    b->thread = NULL;

    b->client->lpVtbl->Stop(b->client);
    b->client->lpVtbl->Reset(b->client);
}

// ─── Device enumeration ──────────────────────────────────────────────────────

HRESULT backend_enumerate_devices(AT_DeviceEnumFn fn, void* user) {
    bool com_owned = false;
    HRESULT hr = com_enter(&com_owned);
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
            read_friendly_name(device, name, sizeof name / sizeof *name);
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
