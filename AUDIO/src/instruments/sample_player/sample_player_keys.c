// ═══════════════════════════════════════════════════════════════════════════
//  instruments/sample_player/sample_player_keys.c — what each key does
//
//  Runs on the control thread; everything reaches the audio thread through
//  sample_player_control.c.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/sample_player/sample_player.h"

#define SEEK_STEP_SECONDS  5.0
#define SPEED_STEP         0.05f

const char SAMPLE_PLAYER_HELP[] =
    "  P play/pause   S stop   L loop   Left/Right  seek -/+ 5 s\n"
    "  Up/Down  pitch -/+ 1 semitone   Y / X  speed -/+ 5 %   R  reset   T  keep tempo on/off\n";

bool sample_player_key(AT_SamplePlayer* p, const AT_KeyEvent* ev) {
    switch (ev->scan_code) {
    // Holding these keys repeats them.
    case AT_SC_UP:
    case AT_SC_DOWN:
        if (ev->down) sample_player_set_pitch(p, p->pitch + (ev->scan_code == AT_SC_UP ? 1.0f : -1.0f));
        return true;

    case AT_SC_Z:       // the key labelled Y on a German keyboard
    case AT_SC_X:
        if (ev->down) sample_player_set_speed(p, p->speed + (ev->scan_code == AT_SC_X ? SPEED_STEP : -SPEED_STEP));
        return true;

    case AT_SC_LEFT:
    case AT_SC_RIGHT:
        if (ev->down) {
            double step = ev->scan_code == AT_SC_RIGHT ? SEEK_STEP_SECONDS : -SEEK_STEP_SECONDS;
            sample_player_seek_seconds(p, sample_player_position_seconds(p) + step);
        }
        return true;

    // These act once per press.
    case AT_SC_P:
        if (ev->down && !ev->repeat) {
            if (sample_player_is_playing(p)) sample_player_pause(p);
            else                              sample_player_play(p);          // also resumes a pause
        }
        return true;

    case AT_SC_S:
        if (ev->down && !ev->repeat) sample_player_stop(p);
        return true;

    case AT_SC_L:
        if (ev->down && !ev->repeat) sample_player_set_loop(p, !p->loop);
        return true;

    case AT_SC_T:
        if (ev->down && !ev->repeat) sample_player_set_keep_tempo(p, !p->keep_tempo);
        return true;

    case AT_SC_R:
        if (ev->down && !ev->repeat) {
            p->pitch = 0.0f;
            p->speed = 1.0f;
            sample_player_set_pitch(p, 0.0f);       // sends both
        }
        return true;

    default:
        return false;       // not ours — the app may use it
    }
}
