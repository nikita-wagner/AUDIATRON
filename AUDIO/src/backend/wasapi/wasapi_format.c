// ═══════════════════════════════════════════════════════════════════════════
//  backend/wasapi/wasapi_format.c — format negotiation + IAudioClient::Initialize
//
//  Shared mode:    we always ask for float32 stereo and let Windows convert.
//  Exclusive mode: the device takes only what its hardware supports, so we
//                  probe a list of formats with IsFormatSupported.
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/wasapi/wasapi_internal.h"

static void make_format(WAVEFORMATEXTENSIBLE* f, AT_SampleType type, uint32_t valid_bits,
                        uint32_t rate, uint32_t channels, DWORD mask) {
    const uint32_t bytes = sample_format_bytes(type);

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

static void store_format(AT_WasapiBackend* b, AT_SampleType type, const WAVEFORMATEXTENSIBLE* f) {
    b->info.sample_type      = type;
    b->info.sample_rate      = f->Format.nSamplesPerSec;
    b->info.channels         = f->Format.nChannels;
    b->info.bytes_per_sample = f->Format.wBitsPerSample / 8;
    b->info.valid_bits       = f->Samples.wValidBitsPerSample;
}

HRESULT wasapi_activate_client(AT_WasapiBackend* b) {
    return b->device->lpVtbl->Activate(b->device, &AT_IID_IAudioClient, CLSCTX_ALL,
                                       NULL, (void**)&b->client);
}

// ─── Shared mode ─────────────────────────────────────────────────────────────

HRESULT wasapi_init_shared(AT_WasapiBackend* b, const AT_BackendConfig* cfg, uint32_t rate) {
    WAVEFORMATEXTENSIBLE fmt;
    make_format(&fmt, AT_SAMPLE_F32, 32, rate, 2, SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);

    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM          // let Windows resample/remap
                | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;

    REFERENCE_TIME duration = (REFERENCE_TIME)cfg->period_us * 10;   // 0 = engine default
    HRESULT hr = b->client->lpVtbl->Initialize(b->client, AUDCLNT_SHAREMODE_SHARED, flags,
                                               duration, 0, (WAVEFORMATEX*)&fmt, NULL);
    if (FAILED(hr)) return hr;

    REFERENCE_TIME def_period = 0, min_period = 0;
    b->client->lpVtbl->GetDevicePeriod(b->client, &def_period, &min_period);
    b->info.period_ms = (double)def_period / REFTIMES_PER_MS;
    store_format(b, AT_SAMPLE_F32, &fmt);
    return S_OK;
}

// ─── Exclusive mode ──────────────────────────────────────────────────────────

typedef struct { AT_SampleType type; uint32_t valid_bits; } FormatCandidate;

static const FormatCandidate k_exclusive_candidates[] = {
    { AT_SAMPLE_F32, 32 },
    { AT_SAMPLE_S32, 32 },
    { AT_SAMPLE_S32, 24 },      // 24-bit in 32-bit container (very common on USB DACs)
    { AT_SAMPLE_S24, 24 },
    { AT_SAMPLE_S16, 16 },
};

HRESULT wasapi_init_exclusive(AT_WasapiBackend* b, const AT_BackendConfig* cfg,
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
            make_format(&fmt, c->type, c->valid_bits, rate, ch, mask);
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
        hr = wasapi_activate_client(b);
        if (FAILED(hr)) return hr;
        hr = b->client->lpVtbl->Initialize(b->client, AUDCLNT_SHAREMODE_EXCLUSIVE,
                                           AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                           period, period, (WAVEFORMATEX*)&fmt, NULL);
    }
    if (FAILED(hr)) return hr;

    b->info.period_ms = (double)period / REFTIMES_PER_MS;
    store_format(b, chosen->type, &fmt);
    return S_OK;
}
