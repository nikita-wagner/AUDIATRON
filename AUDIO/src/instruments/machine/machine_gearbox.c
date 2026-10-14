// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine_gearbox.c — gears, clutch, engine momentum
//
//  A small vehicle model on the control thread, stepped by the machine's tick.
//  It turns the pedal (the machine's throttle) into ENGINE SPEED, which is what
//  the sound's curves read while the gearbox is on.
//
//      engine, clutch open     free_rpm moves toward idle + pedal x (max - idle):
//                              up with time constant rev_up_s, down with rev_down_s
//      vehicle                 speed += (traction - drag - engine braking) x dt;
//                              traction = pedal x power x (this gear's ratio / 1st's),
//                              less near the rev limit, none on it
//      engine, in gear         rpm = speed x ratio; below 1.15 x idle the clutch
//                              slips (pulling away): rpm = max(that, free_rpm)
//
//  A shift, with the settings of the gear it goes INTO:
//      1 clutch out    clutch_out_ms: rpm blends from the wheels' to the free engine's;
//                      the pedal is lifted by `lift`
//      2 shifting      shift_ms: the engine runs free (falls on an upshift; on a
//                      downshift it blips up to the new gear's rpm, rev-matching)
//      3 clutch in     clutch_in_ms: rpm = from + (new gear's rpm - from) x the gear's
//                      MOMENTUM curve over the time (ease out: quick, then settling)
//
//  Automatic: up when rpm reaches up_rpm x the shift-point curve at the pedal,
//  down below down_rpm, kickdown at full pedal when the lower gear has room.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <math.h>
#include <string.h>

#include "core/at_math.h"

const char* const AT_MACHINE_GEARBOX_MODE_NAMES[3] = { "off", "manual", "automatic" };

enum { PHASE_DRIVE, PHASE_CLUTCH_OUT, PHASE_SHIFT, PHASE_CLUTCH_IN };

static AT_MachineCurve shape(AT_MachineShape s, float amount) {
    return (AT_MachineCurve){ 0.0f, 1.0f, s, amount, { 0.25f, 0.5f, 0.75f } };
}

void machine_gearbox_defaults(AT_MachineGearbox* g) {
    static const float ratios[AT_MACHINE_GEARS] = { 130.0f, 78.0f, 55.0f, 42.0f, 34.0f, 28.0f, 24.0f, 21.0f };  // rpm per km/h
    memset(g, 0, sizeof *g);
    g->mode         = AT_MACHINE_GEARBOX_OFF;
    g->gears        = 6;
    g->idle_rpm     = 900.0f;
    g->max_rpm      = 6800.0f;
    g->rev_up_s     = 0.35f;
    g->rev_down_s   = 0.6f;
    g->power        = 16.0f;            // 0 to 50 km/h in about 3.5 s in first
    g->drag         = 0.8f;             // about 185 km/h flat out in 6th
    g->engine_brake = 3.0f;
    g->shift_point  = (AT_MachineCurve){ 0.5f, 1.0f, AT_MACHINE_SHAPE_EASE_IN, 2.0f, { 0.25f, 0.5f, 0.75f } };
    for (int i = 0; i < AT_MACHINE_GEARS; i++) {
        AT_MachineGear* gear = &g->gear[i];
        gear->ratio         = ratios[i];
        gear->up_rpm        = 6200.0f;
        gear->down_rpm      = i == 0 ? 0.0f : 1500.0f;
        gear->clutch_out_ms = 70.0f;
        gear->shift_ms      = 140.0f;
        gear->clutch_in_ms  = 220.0f;
        gear->lift          = 0.85f;
        gear->momentum      = shape(AT_MACHINE_SHAPE_EASE_OUT, 2.0f);
    }
}

static float ratio_of(const AT_MachineGearbox* g, int gear) {
    return gear >= 1 && gear <= g->gears ? g->gear[gear - 1].ratio : 0.0f;
}

// The gear a shift's settings come from: the one it goes into (or, into neutral, the one it leaves).
static const AT_MachineGear* shift_gear(const AT_MachineGearbox* g, const AT_MachineDrive* d) {
    const int n = d->target >= 1 ? d->target : d->gear;
    return &g->gear[(n >= 1 ? n : 1) - 1];
}

float machine_gearbox_shift_rpm(const AT_Machine* m, int gear) {
    const AT_MachineGearbox* g = &m->gearbox;
    if (gear < 1 || gear > g->gears) return g->max_rpm;
    const float up = g->gear[gear - 1].up_rpm * machine_curve_eval(&g->shift_point, m->throttle);
    return at_clampf(up, g->idle_rpm + 300.0f, g->max_rpm);
}

static void start_shift(AT_Machine* m, int target) {
    AT_MachineDrive* d = &m->drive;
    if (d->phase != PHASE_DRIVE || target == d->gear || target < 0 || target > m->gearbox.gears) return;
    d->target  = target;
    d->phase   = PHASE_CLUTCH_OUT;
    d->phase_s = 0.0f;
    d->shifts++;
}

void machine_shift(AT_Machine* m, int direction) {
    if (m->gearbox.mode == AT_MACHINE_GEARBOX_OFF || direction == 0) return;
    int target = m->drive.gear + (direction > 0 ? 1 : -1);
    if (target < 1 && m->gearbox.mode == AT_MACHINE_GEARBOX_AUTOMATIC) target = 1;   // automatic: no neutral by hand
    start_shift(m, target);
}

void machine_set_gearbox_mode(AT_Machine* m, AT_MachineGearboxMode mode) {
    const bool was_off = m->gearbox.mode == AT_MACHINE_GEARBOX_OFF;
    m->gearbox.mode = mode;
    if (mode == AT_MACHINE_GEARBOX_OFF) {
        machine_send_drive(m, m->throttle, true);           // the curves read the throttle again
        return;
    }
    if (was_off) {                                          // a fresh start: standing, in neutral, at idle
        memset(&m->drive, 0, sizeof m->drive);
        m->drive.rpm = m->drive.free_rpm = m->gearbox.idle_rpm;
        m->drive.clutch = 1.0f;
        machine_send_drive(m, 0.0f, true);
    }
}

void machine_drive_step(AT_Machine* m, float dt) {
    AT_MachineGearbox* g = &m->gearbox;
    AT_MachineDrive*   d = &m->drive;
    if (g->mode == AT_MACHINE_GEARBOX_OFF || dt <= 0.0f) return;
    if (g->gears < 1) g->gears = 1;
    if (g->gears > AT_MACHINE_GEARS) g->gears = AT_MACHINE_GEARS;
    const float idle  = g->idle_rpm;
    const float limit = g->max_rpm > idle + 100.0f ? g->max_rpm : idle + 100.0f;

    // manual requests (A / D), also in automatic
    if (m->shift_up)   { machine_shift(m, +1); m->shift_up   = false; }
    if (m->shift_down) { machine_shift(m, -1); m->shift_down = false; }

    const float pedal    = m->running ? m->throttle : 0.0f;
    const bool  shifting = d->phase != PHASE_DRIVE;
    const float eff      = shifting ? pedal * (1.0f - shift_gear(g, d)->lift) : pedal;

    // the free engine: momentum toward the pedal's rpm (on a downshift it blips up to the new gear's)
    float free_target = idle + eff * (limit - idle);
    if (d->phase == PHASE_SHIFT && d->target >= 1 && d->target < d->gear)
        free_target = fmaxf(free_target, fminf(limit, d->speed * ratio_of(g, d->target)));
    const float tau = free_target > d->free_rpm ? g->rev_up_s : g->rev_down_s;
    d->free_rpm += (free_target - d->free_rpm) * (1.0f - expf(-dt / fmaxf(tau, 0.01f)));

    // the vehicle
    const float ratio   = ratio_of(g, d->gear);
    const float engaged = d->gear >= 1 ? d->clutch : 0.0f;
    const float rev_now = at_clampf((d->rpm - idle) / (limit - idle), 0.0f, 1.0f);
    float traction = eff * (1.0f - 0.35f * rev_now * rev_now) * g->power * (ratio / fmaxf(g->gear[0].ratio, 1.0f));
    if (d->rpm >= limit - 1.0f) traction = 0.0f;                          // on the limiter
    const float v100   = d->speed / 100.0f;
    const float drag   = d->speed > 0.0f ? g->drag * (0.25f + 0.75f * v100 * v100) : 0.0f;
    const float brake  = (1.0f - eff) * g->engine_brake * rev_now;
    d->speed += (engaged * traction - drag - engaged * brake) * dt;
    if (d->speed < 0.0f) d->speed = 0.0f;

    const float wheel = d->speed * ratio;                                  // the rpm the wheels would give
    const float slip_below = idle * 1.15f;

    switch (d->phase) {
    case PHASE_DRIVE:
        if (d->gear == 0)
            d->rpm = d->free_rpm;
        else if (wheel < slip_below)
            d->rpm = fmaxf(wheel, d->free_rpm);                           // pulling away: the clutch slips
        else {
            d->rpm = wheel;
            if (d->rpm > limit) {                                         // the rev limiter holds the vehicle too
                d->rpm   = limit;
                d->speed = limit / ratio;
            }
            d->free_rpm = d->rpm;                                         // the engine is locked to the wheels
        }
        d->clutch = 1.0f;

        if (g->mode == AT_MACHINE_GEARBOX_AUTOMATIC && m->running) {
            if (d->gear == 0) {
                if (pedal > 0.02f) start_shift(m, 1);                     // drive: into first
            } else if (d->gear < g->gears && d->rpm >= machine_gearbox_shift_rpm(m, d->gear)) {
                start_shift(m, d->gear + 1);
            } else if (d->gear > 1 && d->rpm < g->gear[d->gear - 1].down_rpm) {
                start_shift(m, d->gear - 1);
            } else if (d->gear > 1 && pedal > 0.9f &&
                       d->speed * ratio_of(g, d->gear - 1) < 0.75f * machine_gearbox_shift_rpm(m, d->gear - 1) &&
                       d->rpm < 0.55f * limit) {
                start_shift(m, d->gear - 1);                              // kickdown
            }
        }
        break;

    case PHASE_CLUTCH_OUT: {
        const float len = fmaxf(shift_gear(g, d)->clutch_out_ms * 0.001f, 0.001f);
        d->phase_s += dt;
        const float u = at_clampf(d->phase_s / len, 0.0f, 1.0f);
        d->clutch = 1.0f - u;
        d->rpm = (d->gear >= 1 ? fmaxf(wheel, idle) : d->free_rpm) * (1.0f - u) + d->free_rpm * u;
        if (u >= 1.0f) { d->phase = PHASE_SHIFT; d->phase_s = 0.0f; }
        break;
    }

    case PHASE_SHIFT: {
        const float len = fmaxf(shift_gear(g, d)->shift_ms * 0.001f, 0.001f);
        d->phase_s += dt;
        d->clutch = 0.0f;
        d->rpm = d->free_rpm;
        if (d->phase_s >= len) {
            d->gear     = d->target;
            d->rpm_from = d->rpm;
            d->phase_s  = 0.0f;
            d->phase    = d->gear >= 1 ? PHASE_CLUTCH_IN : PHASE_DRIVE;   // into neutral: done
        }
        break;
    }

    case PHASE_CLUTCH_IN: {
        const AT_MachineGear* gear = &g->gear[d->gear - 1];
        const float len = fmaxf(gear->clutch_in_ms * 0.001f, 0.001f);
        d->phase_s += dt;
        const float u = at_clampf(d->phase_s / len, 0.0f, 1.0f);
        const float f = machine_shape_eval(&gear->momentum, u);          // the momentum curve
        d->clutch = f;
        const float to = wheel < slip_below ? fmaxf(wheel, d->free_rpm) : fminf(wheel, limit);
        d->rpm = d->rpm_from + (to - d->rpm_from) * f;
        if (u >= 1.0f) {
            d->phase    = PHASE_DRIVE;
            d->target   = d->gear;
            d->free_rpm = d->rpm;
        }
        break;
    }
    }

    d->rpm = at_clampf(d->rpm, idle * 0.9f, limit);
    d->rev = at_clampf((d->rpm - idle) / (limit - idle), 0.0f, 1.0f);
}
