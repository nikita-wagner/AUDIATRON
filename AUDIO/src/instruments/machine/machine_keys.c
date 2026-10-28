// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine_keys.c — the machine's keys and tick (control thread)
//
//  W and S only say "held" or "not held". The instrument's tick (called
//  regularly, whichever track is selected) moves the throttle while they are
//  held, at rise_per_s / fall_per_s (S: with the gearbox on it is the brake,
//  held fully down, and leaves the throttle alone). With the gearbox off the sound follows
//  the throttle; with it on, the tick steps the gearbox (machine_gearbox.c)
//  and the sound follows the engine's rev. Either way every change is sent to
//  the sound, so an input recording has it as it moved.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <math.h>

const char MACHINE_HELP[] =
    "  W  hold: throttle up (starts the machine)     S  hold: the brake (gearbox off: throttle down)\n"
    "  Q  machine on / off     R  throttle to idle (gearbox on: reverse)     X  auto idle on / off (sinks to idle with no key held)\n"
    "  G  gearbox off / manual / automatic     A / D  shift down / up     gearbox on: 1 .. 8 that gear, N neutral, R reverse\n"
    "  1 2 3 4  gearbox off: built-in sound only (your setup stays): petrol engine, jet, propeller, rocket\n"
    "  5 6 7 8  gearbox off: factory setup: that sound with its curves, effects and gearbox     P  save the engine (.atengine)\n";

bool machine_key(AT_Machine* m, const AT_KeyEvent* ev) {
    if (ev->extended) return false;
    const bool press = ev->down && !ev->repeat;
    const bool geared = m->gearbox.mode != AT_MACHINE_GEARBOX_OFF;
    // the gearbox on: 1 .. 8 put that gear in, N neutral, R reverse (straight, by a normal shift)
    if (geared && ev->scan_code >= AT_SC_1 && ev->scan_code <= AT_SC_8) {
        if (press) machine_select_gear(m, ev->scan_code - AT_SC_1 + 1);
        return true;
    }
    if (geared && (ev->scan_code == AT_SC_N || ev->scan_code == AT_SC_R)) {
        if (press) machine_select_gear(m, ev->scan_code == AT_SC_N ? 0 : -1);
        return true;
    }
    switch (ev->scan_code) {
    case AT_SC_W:
        m->key_up = ev->down;
        if (press && !m->running) machine_set_running(m, true);
        return true;
    case AT_SC_S:
        m->key_down = ev->down;
        return true;
    case AT_SC_Q:
        if (press) machine_set_running(m, !m->running);
        return true;
    case AT_SC_R:
        if (press) machine_set_throttle(m, 0.0f);
        return true;
    case AT_SC_X:
        if (press) m->auto_idle = !m->auto_idle;
        return true;
    case AT_SC_G:
        if (press) machine_set_gearbox_mode(m, (AT_MachineGearboxMode)((m->gearbox.mode + 1) % 3));
        return true;
    case AT_SC_A:
        if (press) m->shift_down = true;                    // the tick shifts, between two gearbox steps
        return true;
    case AT_SC_D:
        if (press) m->shift_up = true;
        return true;
    case AT_SC_1: case AT_SC_2: case AT_SC_3: case AT_SC_4:  // another engine: only the sound changes
        if (press) machine_load_builtin(m, (AT_MachineBuiltin)(ev->scan_code - AT_SC_1));
        return true;
    case AT_SC_5: case AT_SC_6: case AT_SC_7: case AT_SC_8:  // the sound and the setup made for it
        if (press) {
            const AT_MachineBuiltin kind = (AT_MachineBuiltin)(ev->scan_code - AT_SC_5);
            if (machine_load_builtin(m, kind)) machine_preset(m, kind);
        }
        return true;
    default:
        return false;
    }
}

void machine_release_all(AT_Machine* m) {
    m->key_up   = false;
    m->key_down = false;
}

void machine_tick(AT_Machine* m) {
    if (!m->engine) return;
    const uint64_t now = engine_now(m->engine);
    double dt = m->last_tick ? (double)(now - m->last_tick) / m->sample_rate : 0.0;
    m->last_tick = now;
    if (dt > 0.25) dt = 0.25;                   // after a stall, do not jump

    // Space (all notes off) stops the sound itself; once the audio thread has had time
    // to apply our own last on / off, follow what it reports.
    if (m->running && !m->sound.shown_running && now > m->running_frame + 4 * AT_MAX_BLOCK_FRAMES)
        m->running = false;

    // S lowers the throttle only with the gearbox off (on, it is the brake: the drive reads it)
    const bool down = m->key_down && m->gearbox.mode == AT_MACHINE_GEARBOX_OFF;
    float t = m->throttle;
    if (m->key_up && !down)                                t += m->rise_per_s * (float)dt;
    else if (down && !m->key_up)                           t -= m->fall_per_s * (float)dt;
    else if (m->auto_idle && !m->driven && !m->key_up && !down) t -= m->idle_per_s * (float)dt;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (fabsf(t - m->throttle) > 1e-4f) machine_set_throttle(m, t);

    if (m->gearbox.mode != AT_MACHINE_GEARBOX_OFF) {
        machine_drive_step(m, (float)dt);
        machine_send_drive(m, m->drive.rev, false);
    } else {
        m->shift_up = m->shift_down = false;
    }
}
