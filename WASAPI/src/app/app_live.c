// ═══════════════════════════════════════════════════════════════════════════
//  app/app_live.c — live mode: device → audio thread → control loop
//
//  The whole lifetime of a live session, in order. Everything that is started
//  here is stopped here, in reverse.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

#include "threads/control/control_thread.h"

int app_run_live(AT_App* app) {
    AT_ControlThread       control;
    AT_ControlThreadConfig control_config = {
        .on_key  = app_on_key,
        .on_tick = app_on_tick,
        .user    = app,
        .tick_ms = 33
    };
    HRESULT hr;
    int     rc = 1;

    // 1. Device first: it decides the sample rate everything else runs at.
    if (!app_open_device(app)) return 1;

    // 2. Engine, instruments, effects.
    if (!app_setup(app, (float)app->backend.sample_rate)) goto close_device;

    // 3. Control thread (this thread): keyboard into raw mode.
    if (!control_thread_init(&control, &control_config)) {
        fprintf(stderr, "stdin is not a console - live mode needs a real terminal.\n");
        goto teardown;
    }

    // 4. Audio thread, then the device: the thread is already waiting when the
    //    device asks for its first buffer.
    if (!audio_thread_start(&app->audio_thread, &app->backend, engine_render, &app->engine)) {
        fprintf(stderr, "could not create the audio thread\n");
        goto shutdown_control;
    }
    hr = backend_start(&app->backend);
    if (FAILED(hr)) {
        fprintf(stderr, "backend_start failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
        goto stop_audio;
    }

    // 5. Run until Esc or the device disappears.
    app_print_help(app);
    control_thread_loop(&control);
    printf("\n");
    rc = 0;

stop_audio:
    audio_thread_stop(&app->audio_thread);      // joins — the engine is idle after this
    backend_stop(&app->backend);
shutdown_control:
    control_thread_shutdown(&control);
teardown:
    app_teardown(app);
close_device:
    backend_close(&app->backend);
    return rc;
}
