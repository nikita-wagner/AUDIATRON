// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_commands.c — applies queued commands (AUDIO THREAD)
//
//  Everything a command touches is owned by the audio thread, so no locks are
//  needed while applying it.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine_commands.h"

#include "core/at_math.h"

static inline bool valid_channel(int ch) { return ch >= 0 && ch < AT_ENGINE_CHANNELS; }

static AT_Equalizer* eq_for(AT_Engine* e, int ch) {
    if (ch == -1)          return &e->master_eq;
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
    }
}

void engine_apply_pending_commands(AT_Engine* e) {
    AT_Command cmd;
    while (at_queue_pop(&e->queue, &cmd))
        apply_command(e, &cmd);
}
