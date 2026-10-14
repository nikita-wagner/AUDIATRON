// ═══════════════════════════════════════════════════════════════════════════
//  app/app_keys.c — what a key press does (control thread)
//
//  Order of who gets a key:
//    0. the input recording logs it; F5 / F6 / F7 (recording) work everywhere
//    1. the instrument on the selected track (notes + its own controls)
//    2. effect hotkeys                       (registered in app_effects.c)
//    3. the track keys and global keys below
//  Every change made here is also logged while recording input.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include "core/at_math.h"

#define VOLUME_STEP 0.1f

static const char* on_off(bool on) { return on ? "on" : "off"; }

// The track that track keys (volume, mute, solo, hold, effects) act on: the selected one.
static uint32_t key_track(const AT_App* app) {
    return app->tracks.selected;
}

// Effects are per track: the hotkey switches the effect on the selected track only.
static bool effect_hotkey(AT_App* app, uint16_t scan) {
    uint32_t track = key_track(app);
    if (track >= app->tracks.count) return false;

    for (uint32_t i = 0; i < app->effect_count; i++) {
        if (app->effects[i].hotkey != scan) continue;
        app_effect_set(app, i, track, !app_effect_enabled(app, i, track));
        app_record_note(app, "track %u %s %s", track + 1, app->effects[i].name, on_off(app_effect_enabled(app, i, track)));
        return true;
    }
    return false;
}

static void change_master_volume(AT_App* app, float delta) {
    app->master_volume = at_clampf(app->master_volume + delta, 0.0f, 2.0f);
    engine_set_master_volume(&app->engine, app->master_volume);
    app_record_note(app, "master volume %.1f", app->master_volume);
}

static void change_track_volume(AT_App* app, float delta) {
    uint32_t t = key_track(app);
    if (t >= app->tracks.count) return;
    const AT_Track* track = &app->tracks.tracks[t];
    track_set_volume(&app->tracks, t, track->volume + delta);
    app_record_note(app, "track %u volume %.1f", t + 1, track->volume);
}

// Mute, solo and hold of the selected track.
static bool track_key(AT_App* app, uint16_t scan) {
    AT_TrackList* tracks = &app->tracks;
    uint32_t      t      = key_track(app);
    if (t >= tracks->count) return false;
    const AT_Track* track = &tracks->tracks[t];
    uint32_t n = t + 1;

    switch (scan) {
    case AT_SC_9:
        track_set_mute(tracks, t, !track->mute);
        app_record_note(app, "track %u mute %s", n, on_off(track->mute));
        return true;
    case AT_SC_0:
        track_set_solo(tracks, t, !track->solo);
        app_record_note(app, "track %u solo %s", n, on_off(track->solo));
        return true;
    case AT_SC_ENTER:
        track_set_hold(tracks, t, !track->hold);
        app_record_note(app, "track %u hold %s", n, on_off(track->hold));
        return true;
    default:
        return false;
    }
}

static bool is_plus(const AT_KeyEvent* ev) {
    return ev->scan_code == AT_SC_RBRACKET || ev->scan_code == AT_SC_EQUALS || ev->scan_code == AT_SC_NUM_PLUS;
}

static bool is_minus(const AT_KeyEvent* ev) {
    return (ev->scan_code == AT_SC_SLASH && !ev->extended) || ev->scan_code == AT_SC_MINUS || ev->scan_code == AT_SC_NUM_MINUS;
}

// E + '+' / '-': the echo of the track the keys act on, longer / shorter.
static void change_echo(AT_App* app, int step) {
    uint32_t t  = key_track(app);
    int      ms = app_set_echo_ms(app, t, app_echo_ms(app, t) + step);
    app_record_note(app, "track %u echo_ms %d", t + 1, ms);
}

// Returns false to quit.
bool app_on_key(void* user, const AT_KeyEvent* ev) {
    AT_App* app = (AT_App*)user;

    app_record_key(app, ev);
    if (ev->down && !ev->repeat && app_record_hotkey(app, ev->scan_code)) return true;

    // E is held down: '+' / '-' set the echo time (E itself still plays as usual)
    if (ev->scan_code == AT_SC_E && !ev->extended) app->e_held = ev->down;
    if (app->e_held && ev->down && (is_plus(ev) || is_minus(ev))) {
        change_echo(app, is_plus(ev) ? AT_APP_ECHO_STEP_MS : -AT_APP_ECHO_STEP_MS);
        return true;                                        // holding the key keeps going
    }
    if (app->e_held && !ev->down && (is_plus(ev) || is_minus(ev))) return true;

    AT_Track* track = track_list_selected(&app->tracks);
    if (track && track->instrument.key && track->instrument.key(track->instrument.self, ev)) return true;

    if (!ev->down) return true;

    // holding these keys repeats them
    switch (ev->scan_code) {
    case AT_SC_N:      change_master_volume(app, -VOLUME_STEP); return true;
    case AT_SC_M:      change_master_volume(app, +VOLUME_STEP); return true;
    case AT_SC_COMMA:  change_track_volume(app, -VOLUME_STEP);  return true;
    case AT_SC_PERIOD: change_track_volume(app, +VOLUME_STEP);  return true;
    default: break;
    }
    if (ev->repeat) return true;

    if (effect_hotkey(app, ev->scan_code)) return true;
    if (track_key(app, ev->scan_code)) return true;

    switch (ev->scan_code) {
    case AT_SC_ESC:
        return false;
    case AT_SC_SPACE:
        engine_all_notes_off(&app->engine, -1);
        for (uint32_t t = 0; t < app->tracks.count; t++)  // the patterns stop too, like the loops
            if (app->track_info[t].kind == AT_APP_KIND_DRUMPAD)
                drumpad_pattern_stop((AT_Drumpad*)app->tracks.tracks[t].instrument.self);
        app_record_note(app, "all notes off");              // the replay stops its loops here too
        break;
    case AT_SC_TAB:
        track_list_select_next(&app->tracks);
        track = track_list_selected(&app->tracks);
        if (track) app_record_note(app, "track %u select %s", app->tracks.selected + 1, track->instrument.name);
        break;
    case AT_SC_P:                                           // the machine's own keys leave P to us
        if (app->tracks.selected < app->tracks.count && app->track_info[app->tracks.selected].kind == AT_APP_KIND_MACHINE)
            app_save_machine(app);
        break;
    case AT_SC_B:
        app->master_eq_bypass = !app->master_eq_bypass;
        engine_set_eq_bypass(&app->engine, -1, app->master_eq_bypass);
        app_record_note(app, "master eq %s", on_off(!app->master_eq_bypass));
        break;
    default:
        break;
    }
    return true;
}
