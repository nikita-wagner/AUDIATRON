// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_pattern.c — the pattern: a step sequencer over the pads
//
//  A grid of bits: row r is pad r, column s is step s. Every step lasts
//  step_ms; on each step, every row whose bit is set hits its pad. The
//  pattern loops until it is stopped.
//
//  Timing: the hits are sent PATTERN_LOOKAHEAD_MS before they are due, each
//  with its exact engine frame (engine_at), so the engine plays them on that
//  frame: step n sounds at start + n × step frames, sample-exact, however the
//  control thread is scheduled. A hit is an ordinary drumpad_hit, so it uses
//  the pad's pitch, cut, wait / play for, effects, and is recorded.
//
//  Control thread only (the instrument's tick calls drumpad_pattern_tick).
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <string.h>

#define PATTERN_LOOKAHEAD_MS  250           // hits are sent this far ahead
#define PATTERN_START_MS      100           // a pattern starts this long after it is started
#define PATTERN_TAG           0x50415454    // marks its timed hits, so a stop can drop them

static double step_frames(const AT_Drumpad* d) {
    return (double)d->pattern.step_ms * (double)d->sample_rate / 1000.0;
}

// Engine frame of step n (counting on through the passes).
static uint64_t step_frame(const AT_Drumpad* d, uint64_t n) {
    return d->pattern.start + (uint64_t)((double)n * step_frames(d) + 0.5);
}

bool drumpad_pattern_set(AT_Drumpad* d, int rows, int steps, const uint32_t* bits, float step_ms) {
    if (rows < 1 || rows > AT_DRUMPAD_PADS || steps < 1 || steps > AT_PATTERN_MAX_STEPS || !(step_ms >= 1.0f)) return false;

    bool was_playing = d->pattern.playing;
    if (was_playing) drumpad_pattern_stop(d);

    AT_DrumPattern* p = &d->pattern;
    memset(p->bits, 0, sizeof p->bits);
    uint32_t mask = steps == 32 ? 0xFFFFFFFFu : (1u << steps) - 1u;
    for (int r = 0; r < rows; r++) p->bits[r] = bits ? bits[r] & mask : 0;
    p->rows    = rows;
    p->steps   = steps;
    p->step_ms = step_ms;

    if (was_playing) drumpad_pattern_play(d);
    return true;
}

void drumpad_pattern_play(AT_Drumpad* d) {
    AT_DrumPattern* p = &d->pattern;
    if (p->rows == 0 || !d->engine) return;
    if (p->playing) drumpad_pattern_stop(d);

    p->start   = engine_now(d->engine) + (uint64_t)(d->sample_rate * PATTERN_START_MS / 1000.0f);
    p->next    = 0;
    p->playing = true;
    drumpad_pattern_tick(d);
}

void drumpad_pattern_stop(AT_Drumpad* d) {
    if (!d->pattern.playing) return;
    d->pattern.playing = false;
    if (d->engine) engine_cancel(d->engine, PATTERN_TAG);      // hits already sent for later frames
}

int drumpad_pattern_step(const AT_Drumpad* d) {
    const AT_DrumPattern* p = &d->pattern;
    if (!p->playing || !d->engine) return -1;
    uint64_t now = engine_now(d->engine);
    if (now < p->start) return -1;
    return (int)((uint64_t)((double)(now - p->start) / step_frames(d)) % (uint64_t)p->steps);
}

void drumpad_pattern_tick(AT_Drumpad* d) {
    AT_DrumPattern* p = &d->pattern;
    if (!p->playing || !d->engine) return;

    const uint64_t horizon = engine_now(d->engine) + (uint64_t)(d->sample_rate * PATTERN_LOOKAHEAD_MS / 1000.0f);
    for (int sent = 0; sent < 4096; sent++) {               // bounded, even with a tiny step time
        uint64_t at = step_frame(d, p->next);
        if (at > horizon) break;

        int step = (int)(p->next % (uint64_t)p->steps);
        engine_at(d->engine, at, PATTERN_TAG);
        for (int r = 0; r < p->rows; r++)
            if (p->bits[r] & (1u << step)) drumpad_hit(d, r, 1.0f);
        engine_at(d->engine, 0, 0);
        p->next++;
    }
}
