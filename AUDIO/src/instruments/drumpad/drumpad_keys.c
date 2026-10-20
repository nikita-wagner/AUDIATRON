// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_keys.c — what each key does
//
//  Runs on the control thread; everything reaches the audio thread through
//  drumpad_control.c.
//
//  A control key (+, -, L, C, ...) changes every pad that is held down at that
//  moment, and only those. With no pad held it does nothing: a change must
//  never land on a pad that was merely hit last.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <stdarg.h>
#include <stdio.h>

#include "input/key_names/key_names.h"

#define VELOCITY      1.0f          // the same level as a loop (its transport voice plays at 1)
#define PITCH_STEP    1.0f          // semitones
#define VOLUME_STEP   0.1f
#define PAN_STEP      0.1f

// Pads 1..8 on the home row, 9..16 on the row above: the default. Every
// drumpad plays the same keys; drumpad_set_pad_keys changes them.
static const uint16_t k_default_keys[AT_DRUMPAD_PADS] = {         // pads 17.. have no key until one is set
    AT_SC_A, AT_SC_S, AT_SC_D, AT_SC_F, AT_SC_G, AT_SC_H, AT_SC_J, AT_SC_K,
    AT_SC_Q, AT_SC_W, AT_SC_E, AT_SC_R, AT_SC_T, AT_SC_Y, AT_SC_U, AT_SC_I
};
static uint16_t s_pad_keys[AT_DRUMPAD_PADS] = {
    AT_SC_A, AT_SC_S, AT_SC_D, AT_SC_F, AT_SC_G, AT_SC_H, AT_SC_J, AT_SC_K,
    AT_SC_Q, AT_SC_W, AT_SC_E, AT_SC_R, AT_SC_T, AT_SC_Y, AT_SC_U, AT_SC_I
};

void drumpad_default_pad_keys(uint16_t keys[AT_DRUMPAD_PADS]) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) keys[i] = k_default_keys[i];
}

void drumpad_set_pad_keys(const uint16_t keys[AT_DRUMPAD_PADS]) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) s_pad_keys[i] = keys ? keys[i] : k_default_keys[i];
}

uint16_t drumpad_pad_key(int pad) {
    return pad >= 0 && pad < AT_DRUMPAD_PADS ? s_pad_keys[pad] : 0;
}

static int find_pad(uint16_t scan) {
    for (int i = 0; scan && i < AT_DRUMPAD_PADS; i++)
        if (s_pad_keys[i] == scan) return i;
    return -1;
}

// ─── Pad keys ────────────────────────────────────────────────────────────────

static void pad_down(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = &d->pads[pad];

    p->held               = true;
    p->changed_while_held = false;
    p->stop_on_release    = false;
    if (!p->loaded) return;

    d->selected = pad;
    if (!p->loop)                         drumpad_hit(d, pad, VELOCITY);
    else if (drumpad_is_looping(d, pad))  p->stop_on_release = true;    // decided on release: this press may be for + / -
    else                                  drumpad_loop_start(d, pad);
}

static void pad_up(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = &d->pads[pad];
    if (!p->held) return;                   // pressed while another track was selected

    p->held = false;
    if (p->stop_on_release && !p->changed_while_held) drumpad_loop_stop(d, pad);
}

// ─── Control keys ────────────────────────────────────────────────────────────

typedef enum {
    CHANGE_PITCH, CHANGE_VOLUME, CHANGE_PAN, CHANGE_LOOP, CHANGE_MODE, CHANGE_RESET,
    CHANGE_SYNC,
    CHANGE_CRUSH, CHANGE_TREMOLO, CHANGE_ECHO           // in AT_PadFx order
} Change;

static void change_pad(AT_Drumpad* d, int pad, Change change, float step) {
    AT_DrumPad* p = &d->pads[pad];

    switch (change) {
    case CHANGE_PITCH:  drumpad_set_pitch (d, pad, p->pitch  + step);      break;
    case CHANGE_VOLUME: drumpad_set_volume(d, pad, p->volume + step);      break;
    case CHANGE_PAN:    drumpad_set_pan   (d, pad, p->pan    + step);      break;
    case CHANGE_MODE:   drumpad_set_keep_tempo(d, pad, !p->keep_tempo);    break;
    case CHANGE_RESET:  drumpad_reset(d, pad);                             break;
    case CHANGE_SYNC:   drumpad_set_sync(d, pad, !p->sync);                break;
    case CHANGE_CRUSH:
    case CHANGE_TREMOLO:
    case CHANGE_ECHO: {
        AT_PadFx fx = (AT_PadFx)(AT_PAD_FX_CRUSH + (change - CHANGE_CRUSH));
        drumpad_set_fx(d, pad, fx, !p->fx[fx]);
        break;
    }
    case CHANGE_LOOP:
        // Held: the loop starts now (replacing the hit this press made).
        // Not held: only the setting changes; the next press starts it.
        if (!p->loop && p->held) drumpad_loop_start(d, pad);
        else                     drumpad_set_loop(d, pad, !p->loop);
        break;
    }
}

// Only the pads held down right now.
static void change_targets(AT_Drumpad* d, Change change, float step) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        AT_DrumPad* p = &d->pads[i];
        if (!p->held || !p->loaded) continue;
        p->changed_while_held = true;
        change_pad(d, i, change, step);
    }
}

// Which control a key is; false = not a control key.
static bool control_of(uint16_t scan, Change* change, float* step, bool* repeats) {
    *repeats = true;                        // holding the key keeps changing the value
    switch (scan) {
    case AT_SC_RBRACKET: case AT_SC_EQUALS: case AT_SC_NUM_PLUS:            // + on German and US layouts
        *change = CHANGE_PITCH;  *step = +PITCH_STEP;  return true;
    case AT_SC_SLASH:    case AT_SC_MINUS:  case AT_SC_NUM_MINUS:           // - on German and US layouts
        *change = CHANGE_PITCH;  *step = -PITCH_STEP;  return true;
    // volume: the arrows, and , . which are the TRACK volume on other tracks
    case AT_SC_UP:   case AT_SC_PERIOD: *change = CHANGE_VOLUME; *step = +VOLUME_STEP; return true;
    case AT_SC_DOWN: case AT_SC_COMMA:  *change = CHANGE_VOLUME; *step = -VOLUME_STEP; return true;
    case AT_SC_RIGHT: *change = CHANGE_PAN;    *step = +PAN_STEP;    return true;
    case AT_SC_LEFT:  *change = CHANGE_PAN;    *step = -PAN_STEP;    return true;
    }

    *repeats = false;                       // toggles act once per press
    *step    = 0.0f;
    switch (scan) {
    case AT_SC_L:         *change = CHANGE_LOOP;  return true;
    case AT_SC_P:         *change = CHANGE_MODE;  return true;
    case AT_SC_BACKSPACE: *change = CHANGE_RESET; return true;
    // the same keys as the app's global effects, but here they act on the pad
    case AT_SC_C:         *change = CHANGE_CRUSH;   return true;
    case AT_SC_V:         *change = CHANGE_TREMOLO; return true;
    case AT_SC_F1:        *change = CHANGE_ECHO;    return true;
    case AT_SC_X:         *change = CHANGE_SYNC;    return true;
    }
    return false;
}

// ─── Entry points ────────────────────────────────────────────────────────────

bool drumpad_key(AT_Drumpad* d, const AT_KeyEvent* ev) {
    int pad = ev->extended ? -1 : find_pad(ev->scan_code);     // arrows share codes with the numpad: never pads

    // the "/" and "*" editors take every key while they are open
    if (d->pat_stage >= 0) return drumpad_pattern_edit_key(d, ev);
    if (d->edit_pad >= 0) return drumpad_cut_edit_key(d, pad, ev);
    if (ev->down && !ev->repeat && ev->ch == '*') {         // the pattern: Shift+'+' (German), numpad '*'
        drumpad_pattern_edit_open(d);
        return true;
    }
    if (ev->down && !ev->repeat && ev->ch == '/') {         // by the character: Shift+7 (German), "/" (US), numpad
        drumpad_cut_edit_start(d);
        return true;
    }

    if (pad >= 0) {
        if (ev->down && !ev->repeat) pad_down(d, pad);
        else if (!ev->down)          pad_up(d, pad);
        return true;
    }

    Change change;
    float  step;
    bool   repeats;
    if (!control_of(ev->scan_code, &change, &step, &repeats)) return false;    // not ours — the app may use it

    if (ev->down && (repeats || !ev->repeat)) change_targets(d, change, step);
    return true;
}

// The keyboard goes to another track: its key-ups will not reach us. Running
// loops keep running, like a song on a player track.
void drumpad_release_all(AT_Drumpad* d) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        d->pads[i].held            = false;
        d->pads[i].stop_on_release = false;
    }
}

// ─── Help text ───────────────────────────────────────────────────────────────

static void append(AT_Drumpad* d, size_t* used, const char* fmt, ...) {
    if (*used >= sizeof d->help) return;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(d->help + *used, sizeof d->help - *used, fmt, args);
    va_end(args);

    if (n > 0) *used += (size_t)n;
    if (*used >= sizeof d->help) *used = sizeof d->help - 1;
}

static void append_key_row(AT_Drumpad* d, size_t* used, int first, int count) {
    for (int i = first; i < first + count; i++) {
        char key[16] = "-";                                  // a pad without a key
        if (s_pad_keys[i]) key_name(s_pad_keys[i], key, sizeof key);
        append(d, used, "%s ", key);
    }
}

void drumpad_build_help(AT_Drumpad* d) {
    size_t used = 0;
    d->help[0]  = '\0';

    append(d, &used, "  ");
    append_key_row(d, &used, 0, 8);
    append(d, &used, " pads 1-8     ");
    append_key_row(d, &used, 8, 8);
    append(d, &used, " pads 9-16\n");

    int shown = 0;
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        if (!d->pads[i].loaded) continue;
        char key[16];
        key_name(s_pad_keys[i], key, sizeof key);
        append(d, &used, "%s%s = %-16.16s", shown % 4 == 0 ? "    " : "  ", key, d->pads[i].label);
        if (++shown % 4 == 0) append(d, &used, "\n");
    }
    if (shown % 4 != 0) append(d, &used, "\n");

    append(d, &used,
           "  hold a pad, then:  + / -  pitch      Up / Down or , .  volume of the pad   Left / Right  pan\n"
           "                     L  loop on/off    P  tape / keep tempo   Backspace  reset the pad\n"
           "                     C  crush   V  tremolo   F1  echo   (on this pad only)\n"
           "                     X  sync: on the shared clock (default) / free\n"
           "                     /  timing in ms: start, end (part of the file), wait, play for\n"
           "  *  pattern (step sequencer): size like 4x4 (pads 1-4, 4 steps), step time, then each row\n"
           "     as 0 / 1 (1 = play); next time only the step time and the rows. Space stops it\n"
           "                        (Enter after each; empty Enter keeps a value; Esc cancels)\n"
           "  these keys only change pads you are holding.   looped pad: press = start / stop\n");
}
