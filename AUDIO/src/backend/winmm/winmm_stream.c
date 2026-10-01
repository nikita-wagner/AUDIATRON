// ═══════════════════════════════════════════════════════════════════════════
//  backend/winmm/winmm_stream.c — stream lifetime + per-period buffer exchange
//
//  A buffer is "free" (ours to fill) while its header lacks WHDR_INQUEUE: either
//  it was never queued, or waveOut has finished playing it. waveOut plays
//  buffers in the order they were written, so the ring position `next` always
//  names the one that becomes free first.
// ═══════════════════════════════════════════════════════════════════════════

#include "backend/winmm/winmm_backend.h"

#include <string.h>

#define CHANNELS         2
#define BYTES_PER_FRAME  (CHANNELS * sizeof(int16_t))
#define MIN_FRAMES       64
#define MIN_BUFFERS      2          // one playing, one waiting — the least that can work

static bool buffer_is_free(const WAVEHDR* header) {
    return (header->dwFlags & WHDR_INQUEUE) == 0;
}

// ─── Open / close ────────────────────────────────────────────────────────────

MMRESULT winmm_open(AT_WinmmBackend* b, const AT_BackendConfig* cfg) {
    memset(b, 0, sizeof *b);

    uint32_t rate   = cfg->sample_rate ? cfg->sample_rate : AT_WINMM_DEFAULT_RATE;
    uint32_t period = cfg->period_us   ? cfg->period_us   : AT_WINMM_DEFAULT_PERIOD_US;
    uint32_t queue  = cfg->queue_us    ? cfg->queue_us    : AT_WINMM_DEFAULT_QUEUE_US;

    // one buffer = one period
    uint32_t frames = (uint32_t)((uint64_t)rate * period / 1000000u);
    if (frames < MIN_FRAMES)                             frames = MIN_FRAMES;
    if (frames > AT_WINMM_POOL_FRAMES / MIN_BUFFERS)     frames = AT_WINMM_POOL_FRAMES / MIN_BUFFERS;

    // as many buffers as it takes to hold the queue, within the ring and the memory pool
    uint32_t queue_frames = (uint32_t)((uint64_t)rate * queue / 1000000u);
    uint32_t count        = (queue_frames + frames / 2) / frames;         // nearest whole number of buffers
    if (count < MIN_BUFFERS)                   count = MIN_BUFFERS;
    if (count > AT_WINMM_MAX_BUFFERS)          count = AT_WINMM_MAX_BUFFERS;
    if (count > AT_WINMM_POOL_FRAMES / frames) count = AT_WINMM_POOL_FRAMES / frames;
    b->buffer_count = count;

    WAVEFORMATEX format;
    memset(&format, 0, sizeof format);
    format.wFormatTag      = WAVE_FORMAT_PCM;
    format.nChannels       = CHANNELS;
    format.nSamplesPerSec  = rate;
    format.wBitsPerSample  = 16;
    format.nBlockAlign     = (WORD)BYTES_PER_FRAME;
    format.nAvgBytesPerSec = rate * (DWORD)BYTES_PER_FRAME;

    // Auto-reset event: waveOut sets it each time a buffer has been played.
    b->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!b->event) return MMSYSERR_NOMEM;

    UINT     device = cfg->device_index >= 0 ? (UINT)cfg->device_index : WAVE_MAPPER;
    MMRESULT mmr    = waveOutOpen(&b->device, device, &format, (DWORD_PTR)b->event, 0, CALLBACK_EVENT);
    if (mmr != MMSYSERR_NOERROR) {
        b->device = NULL;
        winmm_close(b);
        return mmr;
    }

    // Prepared once; a prepared header may be written again and again.
    for (uint32_t i = 0; i < b->buffer_count; i++) {
        WAVEHDR* h = &b->headers[i];
        h->lpData         = (LPSTR)&b->memory[(size_t)i * frames * CHANNELS];
        h->dwBufferLength = frames * (DWORD)BYTES_PER_FRAME;
        mmr = waveOutPrepareHeader(b->device, h, sizeof *h);
        if (mmr != MMSYSERR_NOERROR) {
            winmm_close(b);
            return mmr;
        }
    }
    b->prepared = true;

    b->info.sample_rate      = rate;
    b->info.channels         = CHANNELS;
    b->info.bytes_per_sample = sizeof(int16_t);
    b->info.valid_bits       = 16;
    b->info.sample_type      = AT_SAMPLE_S16;
    b->info.buffer_frames    = frames;
    b->info.period_ms        = 1000.0 * frames / rate;
    b->info.latency_ms       = b->buffer_count * b->info.period_ms;
    b->info.exclusive        = false;
    winmm_device_name(cfg->device_index >= 0 ? device : winmm_default_device(),
                      b->info.device_name, sizeof b->info.device_name);
    return MMSYSERR_NOERROR;
}

void winmm_close(AT_WinmmBackend* b) {
    winmm_stop(b);

    if (b->device) {
        for (uint32_t i = 0; i < b->buffer_count; i++)
            if (b->headers[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(b->device, &b->headers[i], sizeof b->headers[i]);
        waveOutClose(b->device);
        b->device = NULL;
    }
    b->prepared = false;

    if (b->event) { CloseHandle(b->event); b->event = NULL; }
}

// ─── Start / stop ────────────────────────────────────────────────────────────

MMRESULT winmm_start(AT_WinmmBackend* b) {
    if (!b->device || !b->prepared) return MMSYSERR_INVALHANDLE;
    if (b->started) return MMSYSERR_NOERROR;

    b->next    = 0;
    b->written = 0;
    b->failed  = false;
    InterlockedExchange(&b->started, 1);
    SetEvent(b->event);                 // every buffer is free: the filling thread may begin at once
    return MMSYSERR_NOERROR;
}

void winmm_stop(AT_WinmmBackend* b) {
    if (!b->started) return;
    InterlockedExchange(&b->started, 0);
    waveOutReset(b->device);            // stops playback and hands every queued buffer back
}

// ─── Per-period buffer exchange ──────────────────────────────────────────────

bool winmm_wait(AT_WinmmBackend* b, uint32_t timeout_ms) {
    // After a start or an underrun several buffers are free at once, but the
    // event fired only once — so look before sleeping.
    if (b->started && buffer_is_free(&b->headers[b->next])) return true;

    return WaitForSingleObject(b->event, timeout_ms) == WAIT_OBJECT_0;
}

void winmm_wake(AT_WinmmBackend* b) {
    if (b->event) SetEvent(b->event);
}

bool winmm_acquire(AT_WinmmBackend* b, uint8_t** data, uint32_t* frames) {
    *data   = NULL;
    *frames = 0;
    if (b->failed) return false;
    if (!b->started) return true;       // woken before the start: nothing to fill yet

    WAVEHDR* h = &b->headers[b->next];
    if (!buffer_is_free(h)) return true;    // woken for another reason: still playing

    // Once the ring has been filled, at least one buffer should always be
    // queued. If none is, the device has played everything: an audible gap.
    if (b->written >= b->buffer_count) {
        bool any_queued = false;
        for (uint32_t i = 0; i < b->buffer_count; i++)
            if (!buffer_is_free(&b->headers[i])) any_queued = true;
        if (!any_queued) InterlockedIncrement(&b->info.underruns);
    }

    *data   = (uint8_t*)h->lpData;
    *frames = b->info.buffer_frames;
    return true;
}

void winmm_release(AT_WinmmBackend* b, uint32_t frames) {
    WAVEHDR* h = &b->headers[b->next];
    h->dwBufferLength = frames * (DWORD)BYTES_PER_FRAME;

    if (waveOutWrite(b->device, h, sizeof *h) != MMSYSERR_NOERROR) {
        b->failed = true;               // device gone (unplugged, driver removed)
        InterlockedExchange(&b->info.device_lost, 1);
        return;
    }

    b->next = (b->next + 1) % b->buffer_count;
    if (b->written < b->buffer_count) b->written++;
}
