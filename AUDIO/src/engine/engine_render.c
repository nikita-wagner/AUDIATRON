// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine_render.c — the signal graph (AUDIO THREAD)
//
//  Read this file top to bottom to see exactly what happens to a sample on
//  its way from a source to the device. Every stage is one call into another
//  module's folder.
// ═══════════════════════════════════════════════════════════════════════════

#include "engine/engine.h"
#include "engine/engine_commands.h"

static inline void run_hooks(AT_Hook* chain, AT_Buffer* buf) {
    for (int s = 0; s < AT_ENGINE_HOOK_SLOTS; s++)
        if (chain[s].fn) chain[s].fn(buf, chain[s].user);
}

// source → [H] → EQ → [H] → fader/pan → (+=) bus
static void render_channel(AT_Engine* e, int c, AT_Buffer* strip, AT_Buffer* bus) {
    AT_EngineChannel* ch = &e->channel[c];
    if (!ch->source.type) return;                   // nothing plugged in

    at_buffer_clear(strip);
    ch->source.type->render(strip, ch->source.self);

    run_hooks(e->hooks[AT_HOOK_SOURCE][c], strip);
    eq_process(&ch->eq, strip);
    run_hooks(e->hooks[AT_HOOK_CHANNEL][c], strip);
    channel_strip_mix(&ch->strip, strip, bus);
}

// [H] → EQ → fader → [H] → limiter → meter → [H]
static void render_master(AT_Engine* e, AT_Buffer* bus) {
    run_hooks(e->hooks[AT_HOOK_MASTER_PRE][0], bus);
    eq_process(&e->master_eq, bus);
    master_fader_process(&e->master_fader, bus);
    run_hooks(e->hooks[AT_HOOK_MASTER_POST][0], bus);
    if (e->limiter_enabled) soft_clip_process(bus, &e->limiter);
    peak_meter_measure(&e->master_meter, bus);
    run_hooks(e->hooks[AT_HOOK_OUTPUT][0], bus);
}

// One block of at most AT_MAX_BLOCK_FRAMES. `bus` is the (stereo) output.
static void render_block(AT_Engine* e, AT_Buffer* bus) {
    bus->frame_clock = e->frame_clock;
    at_buffer_clear(bus);

    // scratch buffer every channel renders into before it is mixed down
    AT_Buffer strip = {
        .channels    = AT_MAX_CHANNELS,
        .frames      = bus->frames,
        .sample_rate = (uint32_t)e->sample_rate,
        .frame_clock = e->frame_clock
    };
    for (int c = 0; c < AT_MAX_CHANNELS; c++) strip.ch[c] = e->strip_mem[c];

    for (int c = 0; c < AT_ENGINE_CHANNELS; c++)
        render_channel(e, c, &strip, bus);

    render_master(e, bus);

    e->frame_clock += bus->frames;
}

void engine_render(AT_Buffer* out, void* user) {
    AT_Engine* e = (AT_Engine*)user;

    // Commands land at block granularity (one device period, ~3–10 ms).
    // TODO (sample-accurate events): add a frame timestamp to AT_Command and
    // split the block at each event's offset.
    engine_apply_pending_commands(e);

    for (uint32_t done = 0; done < out->frames; ) {
        uint32_t n = out->frames - done;
        if (n > AT_MAX_BLOCK_FRAMES) n = AT_MAX_BLOCK_FRAMES;
        AT_Buffer slice = at_buffer_slice(out, done, n);
        render_block(e, &slice);
        done += n;
    }
}
