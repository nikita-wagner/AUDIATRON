// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_sync.c — one clock for the tracks switched to sync
//
//  The drumpad has always had a shared clock for its pads; this is the same
//  idea for whole channels. Every channel switched to sync is on one clock:
//
//    loops    a loop that starts on a synced channel (a drumpad loop, a song
//             on a player, the machine) asks engine_sync_join where the clock
//             stands and starts there, as if it had started with the clock,
//             so all synced loops play from the same position in time,
//             however late they come in. While nothing synced plays the clock
//             is idle; the next synced loop starts it, from its top.
//    notes    note-ons and note-offs sent "as soon as possible" to a synced
//             channel wait for the clock's next grid step while it runs
//             (engine_set_sync_grid; step 0 = they play at once). A note-off
//             lands at least one step after its note-on, so a quick tap is
//             one step long, never zero. Notes sent with engine_at (a
//             sequencer, a replay) are on their frame already and stay there.
//    origin   a transport can pin the clock to its beat 0
//             (engine_set_sync_origin, fixed): it then runs while the
//             transport runs, whether anything synced plays or not.
//
//  Sources say whether they play through AT_SourceType.playing.
//  Audio thread, except the control functions at the end of the file.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine.h"
#include "engine/engine_commands.h"

#include <math.h>
#include <string.h>

// ─── Audio thread ────────────────────────────────────────────────────────────

bool engine_channel_synced(const AT_Engine* e, uint32_t channel) {
    return e && channel < AT_ENGINE_CHANNELS && e->sync.on[channel];
}

uint64_t engine_sync_origin(const AT_Engine* e) {
    return e->sync.origin;
}

// Is the clock running: pinned by a transport, or something synced plays.
static bool running(const AT_Engine* e) {
    if (e->sync.fixed) return true;
    for (int c = 0; c < AT_ENGINE_CHANNELS; c++) {
        const AT_Source* s = &e->channel[c].source;
        if (e->sync.on[c] && s->type && s->type->playing && s->type->playing(s->self)) return true;
    }
    return false;
}

double engine_sync_join(AT_Engine* e, uint32_t channel) {
    AT_EngineSync* s   = &e->sync;
    const uint64_t now = e->frame_clock;
    (void)channel;

    if (!running(e)) {                                      // the first synced loop starts the clock
        s->origin = now;
        return 0.0;
    }
    if (now >= s->origin) return (double)(now - s->origin);

    // The origin is still ahead (a transport about to start): as if it had
    // started whole bars before it, so it is in phase when the bars begin.
    const double ahead = (double)(s->origin - now);
    if (s->bar > 0.0) return s->bar - fmod(ahead, s->bar);
    return 0.0;
}

// The first grid point at or after `frame`.
static uint64_t grid_at_or_after(const AT_EngineSync* s, uint64_t frame) {
    if (frame <= s->origin) return s->origin;
    const double k = ceil((double)(frame - s->origin) / s->step - 1e-9);
    return s->origin + (uint64_t)llround(k * s->step);
}

bool engine_sync_hold_back(AT_Engine* e, AT_Command* c) {
    if (c->frame != 0 || (c->type != AT_CMD_NOTE_ON && c->type != AT_CMD_NOTE_OFF)) return false;
    AT_EngineSync* s    = &e->sync;
    const int      ch   = c->i[0];
    const int      note = c->i[1] & 127;
    if (ch < 0 || ch >= AT_ENGINE_CHANNELS || !s->on[ch] || s->step <= 0.0 || !running(e)) return false;

    const uint64_t now = e->frame_clock;
    uint64_t       at  = grid_at_or_after(s, now);
    if (c->type == AT_CMD_NOTE_ON) {
        s->note_at[ch][note] = at;
    } else if (s->note_at[ch][note]) {
        const uint64_t after_on = grid_at_or_after(s, s->note_at[ch][note] + 1);   // one step after its note-on
        if (after_on > at) at = after_on;
    }
    if (at <= now) return false;
    c->frame = at;
    c->tag   = 0;
    return true;
}

void engine_sync_apply(AT_Engine* e, const AT_Command* c) {
    AT_EngineSync* s = &e->sync;
    switch (c->type) {
    case AT_CMD_SET_CH_SYNC:
        if (c->i[0] >= 0 && c->i[0] < AT_ENGINE_CHANNELS) s->on[c->i[0]] = c->i[1] != 0;
        break;
    case AT_CMD_SET_SYNC_GRID: {
        uint64_t bits[2];
        for (int k = 0; k < 2; k++) bits[k] = ((uint64_t)(uint32_t)c->i[2 * k + 1] << 32) | (uint32_t)c->i[2 * k];
        memcpy(&s->step, &bits[0], sizeof s->step);
        memcpy(&s->bar,  &bits[1], sizeof s->bar);
        break;
    }
    case AT_CMD_SET_SYNC_ORIGIN:
        if (c->i[0]) s->origin = ((uint64_t)(uint32_t)c->i[2] << 32) | (uint32_t)c->i[1];
        s->fixed = c->i[0] != 0;
        break;
    }
}

void engine_sync_publish(AT_Engine* e) {
    AT_EngineSync* s = &e->sync;
    s->shown_seq++;                                         // odd: being written
    s->shown_origin_lo = (uint32_t)s->origin;
    s->shown_origin_hi = (uint32_t)(s->origin >> 32);
    s->shown_running   = running(e) ? 1 : 0;
    s->shown_seq++;
}

// ─── Control threads ─────────────────────────────────────────────────────────

static bool push(AT_Engine* e, AT_Command c) {
    c.frame = e->stamp_frame;
    c.tag   = e->stamp_tag;
    return at_queue_push(&e->queue, &c);
}

bool engine_set_channel_sync(AT_Engine* e, uint32_t channel, bool on) {
    if (channel >= AT_ENGINE_CHANNELS) return false;
    e->sync.sent_on[channel] = on;
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_SYNC, .i = { (int)channel, on ? 1 : 0 } });
}

bool engine_channel_sync_sent(const AT_Engine* e, uint32_t channel) {
    return channel < AT_ENGINE_CHANNELS && e->sync.sent_on[channel];
}

bool engine_set_sync_grid(AT_Engine* e, double step_frames, double bar_frames) {
    uint64_t bits[2];
    memcpy(&bits[0], &step_frames, sizeof step_frames);
    memcpy(&bits[1], &bar_frames, sizeof bar_frames);
    e->sync.sent_step = step_frames;
    e->sync.sent_bar  = bar_frames;
    return push(e, (AT_Command){ .type = AT_CMD_SET_SYNC_GRID,
                                 .i = { (int32_t)(uint32_t)bits[0], (int32_t)(uint32_t)(bits[0] >> 32),
                                        (int32_t)(uint32_t)bits[1], (int32_t)(uint32_t)(bits[1] >> 32) } });
}

bool engine_set_sync_origin(AT_Engine* e, uint64_t origin, bool fixed) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_SYNC_ORIGIN,
                                 .i = { fixed ? 1 : 0, (int32_t)(uint32_t)origin, (int32_t)(uint32_t)(origin >> 32) } });
}

bool engine_sync_clock(const AT_Engine* e, uint64_t* origin) {
    const AT_EngineSync* s = &e->sync;
    uint32_t seq, lo, hi;
    int32_t  run;
    do {
        seq = s->shown_seq;
        lo  = s->shown_origin_lo;
        hi  = s->shown_origin_hi;
        run = s->shown_running;
    } while ((seq & 1) || seq != s->shown_seq);
    if (origin) *origin = ((uint64_t)hi << 32) | lo;
    return run != 0;
}

uint64_t engine_sync_next_bar(const AT_Engine* e, uint64_t frame) {
    uint64_t origin = 0;
    const double bar = e->sync.sent_bar;
    if (bar <= 0.0 || !engine_sync_clock(e, &origin)) return frame;
    if (frame <= origin) return origin;
    const double k = ceil((double)(frame - origin) / bar - 1e-9);
    return origin + (uint64_t)llround(k * bar);
}
