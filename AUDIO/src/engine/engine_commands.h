// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_commands.h — command codes (private to the engine folder)
//
//  engine_control.c packs these on a control thread, engine_commands.c unpacks
//  and applies them on the audio thread. The comment behind each code is the
//  layout of its AT_Command payload.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "engine/engine.h"

enum {
    AT_CMD_SET_SOURCE,          // i0 = ch, p0 = AT_SourceType*, p1 = self
    AT_CMD_NOTE_ON,             // i0 = ch, i1 = note, f0 = velocity
    AT_CMD_NOTE_OFF,            // i0 = ch, i1 = note
    AT_CMD_ALL_NOTES_OFF,       // i0 = ch or -1
    AT_CMD_SET_CH_HOLD,         // i0 = ch, i1 = hold
    AT_CMD_SET_HOOK,            // i0 = point, i1 = ch, i2 = slot, p0 = fn, p1 = user
    AT_CMD_SET_PARAM,           // i0 = param, f0 = value, p0 = fn, p1 = self
    AT_CMD_SET_CH_VOLUME,       // i0 = ch, f0 = volume
    AT_CMD_SET_CH_PAN,          // i0 = ch, f0 = pan
    AT_CMD_SET_CH_MUTE,         // i0 = ch, i1 = mute
    AT_CMD_SET_MASTER_VOLUME,   // f0 = volume
    AT_CMD_SET_LIMITER,         // i0 = enabled
    AT_CMD_SET_EQ_BAND,         // i0 = ch/-1, i1 = band, i2 = type, f0 = freq, f1 = q, f2 = gain_db
    AT_CMD_SET_EQ_BYPASS        // i0 = ch/-1, i1 = bypass
};

// Audio thread: drains the queue and applies every pending command.
void engine_apply_pending_commands(AT_Engine* e);
