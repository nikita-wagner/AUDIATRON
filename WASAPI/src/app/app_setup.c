// ═══════════════════════════════════════════════════════════════════════════
//  app/app_setup.c — builds the sound setup once the sample rate is known
//
//  Used by both live mode and offline rendering, so both sound the same.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

// The master bus sound: a little warmth and air on everything.
static void master_setup(AT_App* app) {
    app->master_volume    = 0.8f;
    app->master_eq_bypass = false;

    engine_set_eq_band(&app->engine, -1, 0, AT_FILTER_LOWSHELF,  150.0f,  0.707f,  3.0f);
    engine_set_eq_band(&app->engine, -1, 1, AT_FILTER_PEAK,      2500.0f, 1.0f,   -2.0f);
    engine_set_eq_band(&app->engine, -1, 2, AT_FILTER_HIGHSHELF, 9000.0f, 0.707f,  2.0f);
    engine_set_master_volume(&app->engine, app->master_volume);
}

// Switches on the effects named with --fx.
static bool apply_fx_options(AT_App* app) {
    for (uint32_t i = 0; i < app->options.fx_count; i++) {
        int index = app_effect_find(app, app->options.fx[i]);
        if (index < 0) {
            fprintf(stderr, "unknown effect \"%s\". Available:", app->options.fx[i]);
            for (uint32_t k = 0; k < app->effect_count; k++) fprintf(stderr, " %s", app->effects[k].name);
            fprintf(stderr, "\n");
            return false;
        }
        app_effect_set(app, (uint32_t)index, true);
    }
    return true;
}

// Selects the track named with --track.
static bool apply_track_option(AT_App* app) {
    int track = app->options.track;
    if (track >= 1 && track_list_select(&app->tracks, (uint32_t)(track - 1))) return true;

    fprintf(stderr, "no track %d. Tracks:", track);
    for (uint32_t i = 0; i < app->tracks.count; i++)
        fprintf(stderr, " %u=%s", i + 1, app->tracks.tracks[i].instrument.name);
    fprintf(stderr, "\n");
    return false;
}

bool app_setup(AT_App* app, float sample_rate) {
    engine_init(&app->engine, sample_rate);

    master_setup(app);
    app_instruments_setup(app, sample_rate);
    app_effects_setup(app, sample_rate);

    if (!apply_fx_options(app) || !apply_track_option(app)) {
        engine_shutdown(&app->engine);
        return false;
    }
    return true;
}

void app_teardown(AT_App* app) {
    engine_shutdown(&app->engine);
}
