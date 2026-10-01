// ═══════════════════════════════════════════════════════════════════════════
//  threads/audio/audio_thread_loop.c — the real-time loop
//
//  One iteration = one device period:
//
//      wait for the device  →  get its buffer  →  render float audio
//                           →  convert to the device format  →  give it back
//
//  The same loop drives WASAPI and WinMM; the backend decides how long a
//  period is and what format the buffer has.
//
//  Everything called from here must be real-time safe: no malloc, no locks,
//  no printf, no file I/O.
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/audio/audio_thread.h"

#include "io/sample_format/sample_format.h"

#define WAIT_TIMEOUT_MS 2000        // a stalled device must not hang shutdown forever

// Fills `frames` device frames at `dst`, rendering in chunks the DSP can handle.
static void fill_device_buffer(AT_AudioThread* t, uint8_t* dst, uint32_t frames) {
    const AT_BackendInfo* info   = t->backend->info;
    const uint32_t        stride = info->channels * info->bytes_per_sample;

    for (uint32_t done = 0; done < frames; ) {
        uint32_t n = frames - done;
        if (n > AT_MAX_BLOCK_FRAMES) n = AT_MAX_BLOCK_FRAMES;

        AT_Buffer buf = {
            .channels    = AT_MAX_CHANNELS,
            .frames      = n,
            .sample_rate = info->sample_rate,
            .frame_clock = t->frame_clock
        };
        for (uint32_t c = 0; c < AT_MAX_CHANNELS; c++) buf.ch[c] = t->scratch[c];

        if (t->render) t->render(&buf, t->render_user);
        else           at_buffer_clear(&buf);

        sample_format_write(dst + (size_t)done * stride, info->sample_type, info->channels, &buf);

        t->frame_clock += n;
        done += n;
    }
}

void audio_thread_loop(AT_AudioThread* t) {
    AT_Backend* b = t->backend;

    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    const double period_ticks = b->info->period_ms * (double)qpf.QuadPart / 1000.0;

    while (!t->stop) {
        bool ready = backend_wait(b, WAIT_TIMEOUT_MS);
        if (t->stop) break;
        if (!ready) continue;                   // timeout: device stalled, keep waiting

        uint8_t* data   = NULL;
        uint32_t frames = 0;
        if (!backend_acquire(b, &data, &frames)) break;     // stream is dead
        if (frames == 0) continue;

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        fill_device_buffer(t, data, frames);
        QueryPerformanceCounter(&t1);
        if ((double)(t1.QuadPart - t0.QuadPart) > period_ticks)
            InterlockedIncrement(&t->overruns);

        backend_release(b, frames);
    }
}
