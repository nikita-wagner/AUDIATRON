// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_control.c — the public control API (CONTROL THREADS)
//
//  Nothing in this file touches engine state directly: every function packs
//  its arguments into an AT_Command and enqueues it. engine_commands.c is the
//  other end.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine.h"
#include "engine/engine_commands.h"

static bool push(AT_Engine* e, AT_Command c) { return at_queue_push(&e->queue, &c); }

// ─── Sources ─────────────────────────────────────────────────────────────────

bool engine_set_source(AT_Engine* e, uint32_t channel, const AT_SourceType* type, void* self) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_SOURCE, .i = { (int)channel },
                                 .p = { (void*)type, self } });
}

bool engine_note_on(AT_Engine* e, uint32_t channel, int note, float velocity) {
    return push(e, (AT_Command){ .type = AT_CMD_NOTE_ON, .i = { (int)channel, note }, .f = { velocity } });
}

bool engine_note_off(AT_Engine* e, uint32_t channel, int note) {
    return push(e, (AT_Command){ .type = AT_CMD_NOTE_OFF, .i = { (int)channel, note } });
}

bool engine_all_notes_off(AT_Engine* e, int channel) {
    return push(e, (AT_Command){ .type = AT_CMD_ALL_NOTES_OFF, .i = { channel } });
}

bool engine_set_channel_hold(AT_Engine* e, uint32_t channel, bool hold) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_HOLD, .i = { (int)channel, hold } });
}

// ─── Effects / parameters ────────────────────────────────────────────────────

bool engine_set_hook(AT_Engine* e, AT_HookPoint point, uint32_t channel, uint32_t slot,
                     AT_ProcessFn fn, void* user) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_HOOK, .i = { (int)point, (int)channel, (int)slot },
                                 .p = { (void*)fn, user } });
}

bool engine_set_param(AT_Engine* e, AT_ParamFn fn, void* self, int param, float value) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_PARAM, .i = { param }, .f = { value },
                                 .p = { (void*)fn, self } });
}

// ─── Mixer ───────────────────────────────────────────────────────────────────

bool engine_set_channel_volume(AT_Engine* e, uint32_t channel, float volume) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_VOLUME, .i = { (int)channel }, .f = { volume } });
}

bool engine_set_channel_pan(AT_Engine* e, uint32_t channel, float pan) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_PAN, .i = { (int)channel }, .f = { pan } });
}

bool engine_set_channel_mute(AT_Engine* e, uint32_t channel, bool mute) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_MUTE, .i = { (int)channel, mute } });
}

bool engine_set_master_volume(AT_Engine* e, float volume) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_MASTER_VOLUME, .f = { volume } });
}

bool engine_set_limiter(AT_Engine* e, bool enabled) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_LIMITER, .i = { enabled } });
}

// ─── Equalizers ──────────────────────────────────────────────────────────────

bool engine_set_eq_band(AT_Engine* e, int channel, uint32_t band, AT_FilterType type,
                        float freq, float q, float gain_db) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_EQ_BAND, .i = { channel, (int)band, (int)type },
                                 .f = { freq, q, gain_db } });
}

bool engine_set_eq_bypass(AT_Engine* e, int channel, bool bypass) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_EQ_BYPASS, .i = { channel, bypass } });
}

// ─── Metering ────────────────────────────────────────────────────────────────

float engine_master_peak(const AT_Engine* e, uint32_t channel) {
    return peak_meter_read(&e->master_meter, channel);
}

float engine_channel_peak(const AT_Engine* e, uint32_t channel) {
    return channel < AT_ENGINE_CHANNELS ? e->channel[channel].strip.peak : 0.0f;
}
