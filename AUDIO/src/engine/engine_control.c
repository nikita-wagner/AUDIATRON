// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_control.c — the public control API (CONTROL THREADS)
//
//  Nothing in this file touches engine state directly: every function packs
//  its arguments into an AT_Command and enqueues it. engine_commands.c is the
//  other end.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine.h"
#include "engine/engine_commands.h"

// Every command carries the time and tag set with engine_at.
static bool push(AT_Engine* e, AT_Command c) {
    c.frame = e->stamp_frame;
    c.tag   = e->stamp_tag;
    return at_queue_push(&e->queue, &c);
}

static void observe(AT_Engine* e, AT_EngineEvent ev) {
    if (e->observer) e->observer(e->observer_user, &ev);
}

// ─── Timing and observing ────────────────────────────────────────────────────

uint64_t engine_now(const AT_Engine* e) {
    // 64 bits written by the audio thread; on a 32-bit CPU a read can tear, so
    // read until two reads agree.
    const volatile uint64_t* clock = &e->frame_clock;
    uint64_t a, b;
    do { a = *clock; b = *clock; } while (a != b);
    return a;
}

void engine_at(AT_Engine* e, uint64_t frame, int tag) {
    e->stamp_frame = frame;
    e->stamp_tag   = tag;
}

bool engine_cancel(AT_Engine* e, int tag) {
    AT_Command c = { .type = AT_CMD_CANCEL, .i = { tag } };    // not stamped: applies at once
    return at_queue_push(&e->queue, &c);
}

void engine_set_observer(AT_Engine* e, AT_EngineObserver fn, void* user) {
    e->observer      = fn;
    e->observer_user = user;
}

// ─── Sources ─────────────────────────────────────────────────────────────────

bool engine_set_source(AT_Engine* e, uint32_t channel, const AT_SourceType* type, void* self) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_SOURCE, .i = { (int)channel },
                                 .p = { (void*)type, self } });
}

bool engine_note_on(AT_Engine* e, uint32_t channel, int note, float velocity) {
    observe(e, (AT_EngineEvent){ .type = AT_ENGINE_EV_NOTE_ON, .channel = (int)channel, .note = note, .value = velocity });
    return push(e, (AT_Command){ .type = AT_CMD_NOTE_ON, .i = { (int)channel, note }, .f = { velocity } });
}

bool engine_note_off(AT_Engine* e, uint32_t channel, int note) {
    observe(e, (AT_EngineEvent){ .type = AT_ENGINE_EV_NOTE_OFF, .channel = (int)channel, .note = note });
    return push(e, (AT_Command){ .type = AT_CMD_NOTE_OFF, .i = { (int)channel, note } });
}

bool engine_all_notes_off(AT_Engine* e, int channel) {
    observe(e, (AT_EngineEvent){ .type = AT_ENGINE_EV_ALL_NOTES_OFF, .channel = channel });
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
    observe(e, (AT_EngineEvent){ .type = AT_ENGINE_EV_PARAM, .fn = fn, .self = self, .param = param, .value = value });
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

bool engine_set_channel_clock(AT_Engine* e, uint32_t channel, uint64_t offset) {
    return push(e, (AT_Command){ .type = AT_CMD_SET_CH_CLOCK,
                                 .i = { (int)channel, (int32_t)(uint32_t)offset, (int32_t)(uint32_t)(offset >> 32) } });
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
