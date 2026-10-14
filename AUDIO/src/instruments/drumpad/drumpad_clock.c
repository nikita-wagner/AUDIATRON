// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_clock.c — the shared clock, synced and free pads
//
//  Time is the engine's frame_clock (absolute sample count). The drumpad's
//  source wraps its layer stack only to learn where that clock stands: after
//  every block it notes the frame_clock of the next one, which is when any
//  command applied in between takes effect.
//
//  A synced loop that joins a running clock starts as if it had been started
//  with the clock (sampler_play_elapsed): exactly where it would be now, with
//  its cut, pitch and wait / play-for timing. Positions are computed in double
//  from integer frame counts, so phase stays sample-exact.
//
//  Everything here runs on the audio thread.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <math.h>

// ─── The engine source: the layer stack + the clock ──────────────────────────

static void source_render(AT_Buffer* out, void* self) {
    AT_Drumpad* d = (AT_Drumpad*)self;
    layer_stack_render(&d->stack, out);
    d->clock.next = out->frame_clock + out->frames;
}

static void source_note_on(void* self, int note, float velocity) {
    AT_Drumpad* d   = (AT_Drumpad*)self;
    int         pad = note - AT_DRUMPAD_FIRST_NOTE;

    if (pad >= 0 && pad < AT_DRUMPAD_PADS) d->pads[pad].own_origin = d->clock.next;   // a hit starts the pad's own clock
    layer_stack_note_on(&d->stack, note, velocity);
}

static void source_note_off(void* self, int note) { layer_stack_note_off(&((AT_Drumpad*)self)->stack, note); }
static void source_all_off (void* self)           { layer_stack_all_notes_off(&((AT_Drumpad*)self)->stack); }

// Is a synced loop other than `except` playing? Then the clock is running.
// (`playing` is the sampler's own readout, refreshed every block.)
static bool clock_running(const AT_Drumpad* d, int except) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        const AT_DrumPad* p = &d->pads[i];
        if (i != except && p->loaded && p->synced && p->sampler.playing) return true;
    }
    return false;
}

// For the engine's sync clock: a synced loop of the drumpad plays.
static bool source_playing(void* self) { return clock_running((const AT_Drumpad*)self, -1); }

const AT_SourceType AT_DRUMPAD_SOURCE = {
    .name          = "drumpad",
    .render        = source_render,
    .note_on       = source_note_on,
    .note_off      = source_note_off,
    .all_notes_off = source_all_off,
    .playing       = source_playing
};

// ─── Loops on the clock ──────────────────────────────────────────────────────

double drumpad_clock_loop_start(AT_Drumpad* d, int pad) {
    AT_DrumPad*    p   = &d->pads[pad];
    const uint64_t now = d->clock.next;

    p->own_origin = now;
    if (!p->synced) return 0.0;                             // free: from the top, now

    // The track is switched to sync: the clock is the engine's, which every
    // synced track shares, so the pads stay in phase with the other tracks too.
    if (engine_channel_synced(d->engine, d->channel)) {
        const double   elapsed = engine_sync_join(d->engine, d->channel);
        const uint64_t ago     = (uint64_t)llround(elapsed);
        d->clock.origin = d->engine->frame_clock >= ago ? d->engine->frame_clock - ago : 0;
        return elapsed;
    }

    if (!clock_running(d, pad)) {                           // the first synced loop starts the clock
        d->clock.origin = now;
        return 0.0;
    }
    // Join in phase: as if it had started with the clock (the sampler works
    // out the position, with the pad's cut, pitch and timing).
    return (double)(now - d->clock.origin);
}

uint64_t drumpad_clock_origin(const AT_DrumPad* p) {
    return p->synced ? p->clock->origin : p->own_origin;
}
