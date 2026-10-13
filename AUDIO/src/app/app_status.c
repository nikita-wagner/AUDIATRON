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

    printf(" effects (every track has its own; on the drumpad track they switch the held pad's own)\n");
    for (uint32_t i = 0; i < app->effect_count; i++) {
        char key[16];
        key_name(app->effects[i].hotkey, key, sizeof key);
        printf("  %s  %s on/off for the selected track\n", key, app->effects[i].name);
    }

    printf(" recording (files in %s\\, numbered: rec_001.txt / rec_001.wav, never overwritten)\n"
           "  F5  input on/off (keys + drum states as text; stopping it loads it into the replay track)\n"
           "  F6  WAV on/off      F7  both on/off together\n",
           app->options.record_dir ? app->options.record_dir : "recordings");

    printf("  E + '+' / '-'  echo time of the track +/- %d ms (on the drumpad also its pads)\n", AT_APP_ECHO_STEP_MS);

    printf(" global\n"
           "  B  master EQ on/off     N / M  master volume -/+    Space  all notes off\n"
           "  Esc  quit\n\n");
}

// Returns false to quit.
bool app_on_tick(void* user) {
    AT_App* app = (AT_App*)user;

    app_background_tick(app);                               // recorded audio → WAV file, replay lookahead

    char left[METER_WIDTH + 1], right[METER_WIDTH + 1];
    meter_bar_format(left,  METER_WIDTH, engine_master_peak(&app->engine, 0));
    meter_bar_format(right, METER_WIDTH, engine_master_peak(&app->engine, 1));

    // selected track: "2:layers oct 4 piano+strings+synth | trk 0.8 hold mute solo"
    char      track_text[144] = "no tracks";
    AT_Track* track           = track_list_selected(&app->tracks);
    if (track) {
        char instrument_text[80] = "";
        if (track->instrument.status)
            track->instrument.status(track->instrument.self, instrument_text, sizeof instrument_text);

        snprintf(track_text, sizeof track_text, "%u:%s %s | trk %.1f%s%s%s",
                 app->tracks.selected + 1, track->instrument.name, instrument_text, track->volume,
                 track->hold ? " hold" : "", track->mute ? " mute" : "", track->solo ? " solo" : "");
    }

    // effects on the selected track (every track has its own)
    char   effects_text[96] = "";
    size_t used             = 0;
    for (uint32_t i = 0; track && i < app->effect_count; i++) {
        if (!app_effect_enabled(app, i, app->tracks.selected)) continue;
        int n = !strcmp(app->effects[i].name, "echo")
              ? snprintf(effects_text + used, sizeof effects_text - used, "[echo %dms] ", app_echo_ms(app, app->tracks.selected))
              : snprintf(effects_text + used, sizeof effects_text - used, "[%s] ", app->effects[i].name);
        if (n < 0 || (size_t)n >= sizeof effects_text - used) break;        // full
        used += (size_t)n;
    }

    // xruns = blocks rendered too slowly + times the device ran dry
    const AT_BackendInfo* device = app->backend.info;       // NULL while no device is open
    long xruns = app->audio_thread.overruns + (device ? device->underruns : 0);

    // E held: the echo time it changes, also while the echo is off
    if (app->e_held && track && used < sizeof effects_text) {
        uint32_t t = app->tracks.selected;
        if (app->fusion && (int)t == app->fusion_track)
            t = (uint32_t)(fusion_pad_held(app->fusion) ? app->fusion_drum_track : app->fusion_keyboard_track);
        snprintf(effects_text + used, sizeof effects_text - used, "(E: echo %dms on track %u) ", app_echo_ms(app, t), t + 1);
    }

    char record_text[48];
    app_record_status(app, record_text, sizeof record_text);

    char line[360];
    snprintf(line, sizeof line, "%sL[%s] R[%s] %s | vol %.1f %s%s| xruns %ld", record_text,
             left, right, track_text, app->master_volume,
             app->master_eq_bypass ? "EQ-OFF " : "", effects_text, xruns);
    console_text_status_line(line);

    if (device && device->device_lost) {
        printf("\nDevice lost (unplugged or format changed). Exiting.\n");
        return false;
    }
    return true;
}
