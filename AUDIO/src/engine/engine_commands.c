// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_commands.c — applies queued commands (AUDIO THREAD)
//
//  Everything a command touches is owned by the audio thread, so no locks are
//  needed while applying it.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine_commands.h"

#include <string.h>

#include "core/at_math.h"

static inline bool valid_channel(int ch) { return ch >= 0 && ch < AT_ENGINE_CHANNELS; }

static inline bool valid_bus(int b) { return b >= 0 && b < AT_ENGINE_BUSES; }

static AT_Equalizer* eq_for(AT_Engine* e, int ch) {
    if (ch == -1)          return &e->master_eq;
    if (ch <= -2)          return valid_bus(-2 - ch) ? &e->bus[-2 - ch].eq : NULL;
    if (valid_channel(ch)) return &e->channel[ch].eq;
    return NULL;
}

static void all_notes_off(AT_Source* s) {
    if (s->type && s->type->all_notes_off) s->type->all_notes_off(s->self);
}

static void apply_command(AT_Engine* e, const AT_Command* c) {
    int ch = c->i[0];

    switch (c->type) {
    case AT_CMD_SET_SOURCE:
        if (valid_channel(ch))
            e->channel[ch].source = (AT_Source){ (const AT_SourceType*)c->p[0], c->p[1] };
        break;

    case AT_CMD_NOTE_ON:
        if (valid_channel(ch)) {
            AT_Source* s = &e->channel[ch].source;
            if (s->type && s->type->note_on) s->type->note_on(s->self, c->i[1], c->f[0]);
        }
        break;

    case AT_CMD_NOTE_OFF:
        if (valid_channel(ch) && !e->channel[ch].hold) {
            AT_Source* s = &e->channel[ch].source;
            if (s->type && s->type->note_off) s->type->note_off(s->self, c->i[1]);
        }
        break;

    case AT_CMD_ALL_NOTES_OFF:
        for (int i = 0; i < AT_ENGINE_CHANNELS; i++)
            if (ch < 0 || ch == i) all_notes_off(&e->channel[i].source);
        break;

    case AT_CMD_SET_CH_HOLD:
        if (valid_channel(ch)) {
            bool hold = c->i[1] != 0;
            if (e->channel[ch].hold && !hold) all_notes_off(&e->channel[ch].source);
            e->channel[ch].hold = hold;
        }
        break;

    case AT_CMD_SET_HOOK: {
        int point = c->i[0], hook_ch = c->i[1], slot = c->i[2];
        if (point < 0 || point >= AT_HOOK_POINT_COUNT) break;
        if (point >= AT_HOOK_MASTER_PRE) hook_ch = 0;
        if (!valid_channel(hook_ch) || slot < 0 || slot >= AT_ENGINE_HOOK_SLOTS) break;
        e->hooks[point][hook_ch][slot] = (AT_Hook){ (AT_ProcessFn)c->p[0], c->p[1] };
        break;
    }

    case AT_CMD_SET_PARAM: {
        AT_ParamFn fn = (AT_ParamFn)c->p[0];
        if (fn) fn(c->p[1], c->i[0], c->f[0]);
        break;
    }

    case AT_CMD_SET_CH_VOLUME:
        if (valid_channel(ch)) e->channel[ch].strip.volume = at_clampf(c->f[0], 0.0f, 2.0f);
        break;

    case AT_CMD_SET_CH_PAN:
        if (valid_channel(ch)) e->channel[ch].strip.pan = at_clampf(c->f[0], -1.0f, 1.0f);
        break;

    case AT_CMD_SET_CH_MUTE:
        if (valid_channel(ch)) e->channel[ch].strip.mute = c->i[1] != 0;
        break;

    case AT_CMD_SET_MASTER_VOLUME:
        e->master_fader.volume = at_clampf(c->f[0], 0.0f, 2.0f);
        break;

    case AT_CMD_SET_LIMITER:
        e->limiter_enabled = c->i[0] != 0;
        break;

    case AT_CMD_SET_EQ_BAND: {
        AT_Equalizer* eq = eq_for(e, ch);
        if (eq) eq_set_band(eq, (uint32_t)c->i[1], (AT_FilterType)c->i[2], c->f[0], c->f[1], c->f[2]);
        break;
    }

    case AT_CMD_SET_EQ_BYPASS: {
        AT_Equalizer* eq = eq_for(e, ch);
        if (eq) eq->bypass = c->i[1] != 0;
        break;
    }

    case AT_CMD_SET_CH_CLOCK:
        if (valid_channel(ch))
            e->channel[ch].clock_offset = ((uint64_t)(uint32_t)c->i[2] << 32) | (uint32_t)c->i[1];
        break;

    case AT_CMD_SET_CH_BUS:
        if (valid_channel(ch)) {
            memset(e->channel[ch].route, AT_ROUTE_OFF, sizeof e->channel[ch].route);
            e->channel[ch].route[valid_bus(c->i[1]) ? 1 + c->i[1] : 0] = AT_ROUTE_STEREO;
        }
        break;

    case AT_CMD_SET_CH_ROUTE:
        if (valid_channel(ch) && (c->i[1] == -1 || valid_bus(c->i[1])) && c->i[2] >= AT_ROUTE_OFF && c->i[2] <= AT_ROUTE_RIGHT)
            e->channel[ch].route[1 + c->i[1]] = (uint8_t)c->i[2];
        break;

    case AT_CMD_SET_BUS_VOLUME:
        if (valid_bus(ch)) e->bus[ch].volume = at_clampf(c->f[0], 0.0f, 2.0f);
        break;

    case AT_CMD_SET_BUS_MUTE:
        if (valid_bus(ch)) e->bus[ch].mute = c->i[1] != 0;
        break;

    case AT_CMD_SET_CH_SYNC:
    case AT_CMD_SET_SYNC_GRID:
    case AT_CMD_SET_SYNC_ORIGIN:
        engine_sync_apply(e, c);
        break;

    case AT_CMD_CANCEL: {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < e->timed_count; i++)
            if (e->timed[i].tag != c->i[0]) e->timed[kept++] = e->timed[i];
        e->timed_count = kept;
        break;
    }
    }
}

// ─── Timed commands ──────────────────────────────────────────────────────────

// Into the waiting list, after every command with the same or an earlier
// frame, so commands for one frame keep the order they were sent in. A full
// list cannot wait: the command is applied now (late, but not lost).
static void wait_for_frame(AT_Engine* e, const AT_Command* c) {
    if (e->timed_count == AT_ENGINE_TIMED) { apply_command(e, c); return; }

    uint32_t at = e->timed_count;
    while (at > 0 && e->timed[at - 1].frame > c->frame) at--;
    memmove(&e->timed[at + 1], &e->timed[at], (e->timed_count - at) * sizeof *e->timed);
    e->timed[at] = *c;
    e->timed_count++;
}

void engine_apply_due_commands(AT_Engine* e) {
    uint32_t due = 0;
    while (due < e->timed_count && e->timed[due].frame <= e->frame_clock) {
        AT_Command c = e->timed[due++];
        apply_command(e, &c);
    }
    if (due == 0) return;
    e->timed_count -= due;
    memmove(&e->timed[0], &e->timed[due], e->timed_count * sizeof *e->timed);
}

uint32_t engine_frames_to_next_command(const AT_Engine* e, uint32_t limit) {
    if (e->timed_count == 0) return limit;
    uint64_t until = e->timed[0].frame - e->frame_clock;    // > 0: due ones were applied
    return until < limit ? (uint32_t)until : limit;
}

void engine_apply_pending_commands(AT_Engine* e) {
    engine_apply_due_commands(e);                           // what was waiting comes first

    AT_Command cmd;
    while (at_queue_pop(&e->queue, &cmd)) {
        if (cmd.frame > e->frame_clock)            wait_for_frame(e, &cmd);
        else if (engine_sync_hold_back(e, &cmd))   wait_for_frame(e, &cmd);     // a synced note waits for the grid
        else                                       apply_command(e, &cmd);
    }
}
