// ═══════════════════════════════════════════════════════════════════════════
//  app/app_offline.c — --offline: render a demo into a WAV file, no device
//
//  Same engine, same tracks, same effects as live mode, but this thread
//  calls engine_render itself instead of an audio thread. The quickest way to
//  test a new effect or sound: render, then look at / listen to the file.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

#include "core/at_math.h"
#include "io/wav_writer/wav_writer.h"

#define BLOCK_FRAMES    480
#define DEMO_VELOCITY   0.9f

static const int k_arpeggio[] = { 48, 55, 60, 64, 67, 72, 67, 64, 60, 55 };
#define ARPEGGIO_LENGTH ((int)(sizeof k_arpeggio / sizeof *k_arpeggio))

int app_run_offline(AT_App* app) {
    const AT_AppOptions* o    = &app->options;
    const uint32_t       rate = o->sample_rate ? o->sample_rate : 48000;

    if (!app_setup(app, (float)rate)) return 1;

    // the demo plays on the selected track (--track N, default: the first)
    const AT_Track* track = track_list_selected(&app->tracks);
    if (!track) {
        fprintf(stderr, "no tracks registered - nothing to render\n");
        app_teardown(app);
        return 1;
    }
    const uint32_t demo_channel = track->channel;

    AT_WavWriter wav;
    if (!wav_writer_open(&wav, o->offline_path, rate, AT_MAX_CHANNELS)) {
        fprintf(stderr, "cannot open %s\n", o->offline_path);
        app_teardown(app);
        return 1;
    }

    const uint32_t total       = (uint32_t)(o->offline_seconds * rate);
    const uint32_t step_frames = rate / 4;                      // new note every 250 ms

    float  mem[AT_MAX_CHANNELS][BLOCK_FRAMES];
    int    step   = -1;
    float  peak   = 0.0f;
    double sum_sq = 0.0;

    at_enable_flush_to_zero();

    for (uint32_t done = 0; done < total; ) {
        // the "player": one note of the arpeggio at a time
        int want_step = (int)(done / step_frames);
        if (want_step != step) {
            if (step >= 0) engine_note_off(&app->engine, demo_channel, k_arpeggio[step % ARPEGGIO_LENGTH]);
            step = want_step;
            engine_note_on(&app->engine, demo_channel, k_arpeggio[step % ARPEGGIO_LENGTH], DEMO_VELOCITY);
        }

        uint32_t  n   = total - done < BLOCK_FRAMES ? total - done : BLOCK_FRAMES;
        AT_Buffer buf = { .channels = AT_MAX_CHANNELS, .frames = n, .sample_rate = rate };
        for (int c = 0; c < AT_MAX_CHANNELS; c++) buf.ch[c] = mem[c];

        engine_render(&buf, &app->engine);

        for (uint32_t c = 0; c < buf.channels; c++)
            for (uint32_t i = 0; i < n; i++) {
                float x = buf.ch[c][i];
                peak    = fmaxf(peak, fabsf(x));
                sum_sq += (double)x * x;
            }

        if (!wav_writer_write(&wav, &buf)) {
            fprintf(stderr, "write error on %s\n", o->offline_path);
            break;
        }
        done += n;
    }

    wav_writer_close(&wav);
    app_teardown(app);

    printf("Rendered %.2f s @ %u Hz to %s - peak %.1f dBFS, RMS %.1f dBFS\n",
           o->offline_seconds, rate, o->offline_path, at_gain_to_db(peak),
           at_gain_to_db((float)sqrt(sum_sq / ((double)AT_MAX_CHANNELS * total))));
    return 0;
}
