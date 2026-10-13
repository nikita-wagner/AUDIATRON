// ═══════════════════════════════════════════════════════════════════════════
//  instruments/replay/replay_keys.c — what each key does (control thread)
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/replay/replay.h"

const char REPLAY_HELP[] =
    "  P  play the recording from the start / stop      L  loop it on/off\n"
    "  Left / Right (or Up / Down)  previous / next recording in its folder\n"
    "  a recording loads here when you stop it (F5 / F7), or start with --replay FILE\n";

bool replay_key(AT_Replay* r, const AT_KeyEvent* ev) {
    switch (ev->scan_code) {
    case AT_SC_P:
        if (ev->down && !ev->repeat) {
            if (replay_is_playing(r)) replay_stop(r);
            else                      replay_play(r);
        }
        return true;

    case AT_SC_L:
        if (ev->down && !ev->repeat) replay_set_loop(r, !r->loop);
        return true;

    case AT_SC_LEFT:
    case AT_SC_UP:
    case AT_SC_RIGHT:
    case AT_SC_DOWN:
        if (ev->down && !ev->repeat)
            replay_select(r, ev->scan_code == AT_SC_RIGHT || ev->scan_code == AT_SC_DOWN ? +1 : -1);
        return true;

    default:
        return false;       // not ours — the app may use it
    }
}
