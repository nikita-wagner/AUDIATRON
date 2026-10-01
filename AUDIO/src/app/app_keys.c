// ═══════════════════════════════════════════════════════════════════════════
//  app/app_keys.c — what a key press does (control thread)
//
//  Order of who gets a key:
//    1. the instrument on the selected track (notes + its own controls)
//    2. effect hotkeys                       (registered in app_effects.c)
//    3. the track keys and global keys below
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include "core/at_math.h"

#define VOLUME_STEP 0.1f

static bool effect_hotkey(AT_App* app, uint16_t scan) {
    for (uint32_t i = 0; i < app->effect_count; i++) {
        if (app->effects[i].hotkey != scan) continue;
        app_effect_set(app, i, !app->effects[i].enabled);
        return true;
    }
    return false;
}

static void change_master_volume(AT_App* app, float delta) {
    app->master_volume = at_clampf(app->master_volume + delta, 0.0f, 2.0f);
    engine_set_master_volume(&app->engine, app->master_volume);
}

static void change_track_volume(AT_App* app, float delta) {
    const AT_Track* track = track_list_selected(&app->tracks);
    if (track) track_set_volume(&app->tracks, app->tracks.selected, track->volume + delta);
}

// Mute, solo and hold of the selected track.
static bool track_key(AT_App* app, uint16_t scan) {
    AT_TrackList*   tracks = &app->tracks;
    const AT_Track* track  = track_list_selected(tracks);
    if (!track) return false;

    switch (scan) {
    case AT_SC_9:     track_set_mute(tracks, tracks->selected, !track->mute); return true;
    case AT_SC_0:     track_set_solo(tracks, tracks->selected, !track->solo); return true;
    case AT_SC_ENTER: track_set_hold(tracks, tracks->selected, !track->hold); return true;
    default:          return false;
    }
}

// Returns false to quit.
bool app_on_key(void* user, const AT_KeyEvent* ev) {
    AT_App* app = (AT_App*)user;

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
        break;
    case AT_SC_TAB:
        track_list_select_next(&app->tracks);
        break;
    case AT_SC_B:
        app->master_eq_bypass = !app->master_eq_bypass;
        engine_set_eq_bypass(&app->engine, -1, app->master_eq_bypass);
        break;
    default:
        break;
    }
    return true;
}
