// ═══════════════════════════════════════════════════════════════════════════
//  app/app_status.c — help text + the live status line (control thread)
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

#include "input/key_names/key_names.h"
#include "ui/console_text/console_text.h"
#include "ui/meter_bar/meter_bar.h"

#define METER_WIDTH 10

void app_print_help(const AT_App* app) {
    printf("\nKeys\n");

    for (uint32_t i = 0; i < app->tracks.count; i++) {
        const AT_Instrument* instrument = &app->tracks.tracks[i].instrument;
        printf(" track %u: %s\n", i + 1, instrument->name);
        if (instrument->help) fputs(instrument->help, stdout);
    }

    printf(" tracks\n"
           "  Tab  next track         , / .  track volume -/+     Enter  hold (notes keep sounding)\n"
           "  9  mute track           0  solo track\n");

    printf(" effects\n");
    for (uint32_t i = 0; i < app->effect_count; i++) {
        char key[16];
        key_name(app->effects[i].hotkey, key, sizeof key);
        printf("  %s  %s on/off\n", key, app->effects[i].name);
    }

    printf(" global\n"
           "  B  master EQ on/off     N / M  master volume -/+    Space  all notes off\n"
           "  Esc  quit\n\n");
}

// Returns false to quit.
bool app_on_tick(void* user) {
    AT_App* app = (AT_App*)user;

    char left[METER_WIDTH + 1], right[METER_WIDTH + 1];
    meter_bar_format(left,  METER_WIDTH, engine_master_peak(&app->engine, 0));
    meter_bar_format(right, METER_WIDTH, engine_master_peak(&app->engine, 1));

    // selected track: "2:layers oct 4 piano+strings+synth | trk 0.8 hold mute solo"
    char      track_text[112] = "no tracks";
    AT_Track* track           = track_list_selected(&app->tracks);
    if (track) {
        char instrument_text[56] = "";
        if (track->instrument.status)
            track->instrument.status(track->instrument.self, instrument_text, sizeof instrument_text);

        snprintf(track_text, sizeof track_text, "%u:%s %s | trk %.1f%s%s%s",
                 app->tracks.selected + 1, track->instrument.name, instrument_text, track->volume,
                 track->hold ? " hold" : "", track->mute ? " mute" : "", track->solo ? " solo" : "");
    }

    // active effects
    char   effects_text[96] = "";
    size_t used             = 0;
    for (uint32_t i = 0; i < app->effect_count; i++) {
        if (!app->effects[i].enabled) continue;
        int n = snprintf(effects_text + used, sizeof effects_text - used, "[%s] ", app->effects[i].name);
        if (n < 0 || (size_t)n >= sizeof effects_text - used) break;        // full
        used += (size_t)n;
    }

    char line[320];
    snprintf(line, sizeof line, "L[%s] R[%s] %s | vol %.1f %s%s| xruns %ld",
             left, right, track_text, app->master_volume,
             app->master_eq_bypass ? "EQ-OFF " : "", effects_text,
             app->audio_thread.overruns);
    console_text_status_line(line);

    if (app->backend.device_lost) {
        printf("\nDevice lost (unplugged or format changed). Exiting.\n");
        return false;
    }
    return true;
}
