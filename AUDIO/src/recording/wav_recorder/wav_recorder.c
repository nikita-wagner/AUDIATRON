#include "recording/wav_recorder/wav_recorder.h"

#include <stdlib.h>
#include <string.h>

#define RING_MASK (AT_WAV_RECORDER_FRAMES - 1)
_Static_assert((AT_WAV_RECORDER_FRAMES & RING_MASK) == 0, "AT_WAV_RECORDER_FRAMES must be a power of two");

// Publishing an index to the other thread: the Interlocked functions are full
// barriers and exist on every Windows since 95 and in every compiler's headers.
#define LOAD_INDEX(p)      ((uint32_t)InterlockedExchangeAdd((p), 0))
#define STORE_INDEX(p, v)  InterlockedExchange((p), (LONG)(v))

bool wav_recorder_open(AT_WavRecorder* r, const char* path, uint32_t sample_rate) {
    memset(r, 0, sizeof *r);
    r->ring = (float*)malloc(sizeof(float) * 2 * AT_WAV_RECORDER_FRAMES);
    if (!r->ring) return false;
    if (!wav_writer_open(&r->wav, path, sample_rate, 2)) {
        free(r->ring);
        r->ring = NULL;
        return false;
    }
    return true;
}

// ─── Audio thread ────────────────────────────────────────────────────────────

void wav_recorder_process(AT_Buffer* buf, void* user) {
    AT_WavRecorder* r = (AT_WavRecorder*)user;

    if (!r->started) {
        r->first_frame = buf->frame_clock;
        STORE_INDEX(&r->started, 1);                        // published before any frame
    }

    uint32_t write = (uint32_t)r->write;                    // only this thread changes it
    uint32_t space = AT_WAV_RECORDER_FRAMES - (write - LOAD_INDEX(&r->read));
    uint32_t n     = buf->frames < space ? buf->frames : space;

    const float* left  = buf->ch[0];
    const float* right = buf->ch[buf->channels > 1 ? 1 : 0];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = (write + i) & RING_MASK;
        r->ring[slot * 2]     = left[i];
        r->ring[slot * 2 + 1] = right[i];
    }
    if (n < buf->frames) InterlockedExchangeAdd(&r->dropped, (LONG)(buf->frames - n));
    STORE_INDEX(&r->write, write + n);
}

// ─── Control thread ──────────────────────────────────────────────────────────

uint32_t wav_recorder_flush(AT_WavRecorder* r) {
    if (!r->ring || !r->wav.file) return 0;

    uint32_t read  = (uint32_t)r->read;                     // only this thread changes it
    uint32_t avail = LOAD_INDEX(&r->write) - read;
    uint32_t total = avail;

    while (avail > 0) {
        uint32_t n = avail < AT_MAX_BLOCK_FRAMES ? avail : AT_MAX_BLOCK_FRAMES;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t slot = (read + i) & RING_MASK;
            r->chunk[0][i] = r->ring[slot * 2];
            r->chunk[1][i] = r->ring[slot * 2 + 1];
        }
        AT_Buffer b = { .channels = 2, .frames = n, .sample_rate = r->wav.sample_rate };
        b.ch[0] = r->chunk[0];
        b.ch[1] = r->chunk[1];
        wav_writer_write(&r->wav, &b);

        read  += n;
        avail -= n;
        STORE_INDEX(&r->read, read);                        // frees the space for the audio thread
    }
    return total;
}

void wav_recorder_close(AT_WavRecorder* r) {
    wav_recorder_flush(r);
    if (r->wav.file) wav_writer_close(&r->wav);
    free(r->ring);
    r->ring = NULL;
}
