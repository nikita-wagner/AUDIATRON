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
    strip->frame_clock = e->frame_clock - ch->clock_offset;     // the channel's own time (0 offset: the engine's)
    ch->source.type->render(strip, ch->source.self);

    run_hooks(e->hooks[AT_HOOK_SOURCE][c], strip);
    eq_process(&ch->eq, strip);
    run_hooks(e->hooks[AT_HOOK_CHANNEL][c], strip);
    channel_strip_mix(&ch->strip, strip, bus);
}

#define BUS_GLIDE 64             // frames a bus fader glides over to a new value (1.3 ms at 48 kHz): no click, on time

// A bus: its channels' sum → EQ → [H] → volume (gliding, so a change does not click) → meter → (+=) master
static void render_bus(AT_Engine* e, int b, AT_Buffer* sum, AT_Buffer* master) {
    AT_EngineBus* bus = &e->bus[b];
    eq_process(&bus->eq, sum);
    run_hooks(e->hooks[AT_HOOK_BUS][b], sum);
    const float target = bus->mute ? 0.0f : bus->volume;
    const uint32_t glide = sum->frames < BUS_GLIDE ? sum->frames : BUS_GLIDE;
    const float from = bus->gain, step = glide ? (target - from) / (float)glide : 0.0f;
    for (uint32_t c = 0; c < sum->channels && c < master->channels; c++) {
        float g = from;
        for (uint32_t i = 0; i < sum->frames; i++) {
            g = i < glide ? g + step : target;
            sum->ch[c][i] *= g;
            master->ch[c][i] += sum->ch[c][i];
        }
    }
    bus->gain = target;
    peak_meter_measure(&bus->meter, sum);
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

    // the buses that have channels: cleared, then their channels are summed into them
    AT_Buffer sums[AT_ENGINE_BUSES];
    bool used[AT_ENGINE_BUSES] = { false };
    for (int c = 0; c < AT_ENGINE_CHANNELS; c++) {
        int b = e->channel[c].bus;
        if (b < 0 || b >= AT_ENGINE_BUSES || used[b]) continue;
        used[b] = true;
        sums[b] = (AT_Buffer){ .channels = bus->channels < AT_MAX_CHANNELS ? bus->channels : AT_MAX_CHANNELS, .frames = bus->frames,
                               .sample_rate = bus->sample_rate, .frame_clock = e->frame_clock };
        for (uint32_t k = 0; k < bus->channels && k < AT_MAX_CHANNELS; k++) sums[b].ch[k] = e->bus_mem[b][k];
        at_buffer_clear(&sums[b]);
    }

    for (int c = 0; c < AT_ENGINE_CHANNELS; c++) {
        int b = e->channel[c].bus;
        render_channel(e, c, &strip, b >= 0 && b < AT_ENGINE_BUSES ? &sums[b] : bus);
    }
    for (int b = 0; b < AT_ENGINE_BUSES; b++)
        if (used[b]) render_bus(e, b, &sums[b], bus);
        else for (int k = 0; k < AT_MAX_CHANNELS; k++) e->bus[b].meter.peak[k] = 0.0f;   // no channels: silent

    render_master(e, bus);

    e->frame_clock += bus->frames;
}

void engine_render(AT_Buffer* out, void* user) {
    AT_Engine* e = (AT_Engine*)user;

    // Plain commands land at the start of the block (one device period,
    // ~3–10 ms). Timed ones (engine_at) land on their exact frame: the block
    // is rendered in pieces that end where the next timed command is due.
    engine_apply_pending_commands(e);

    for (uint32_t done = 0; done < out->frames; ) {
        engine_apply_due_commands(e);

        uint32_t n = out->frames - done;
        if (n > AT_MAX_BLOCK_FRAMES) n = AT_MAX_BLOCK_FRAMES;
        n = engine_frames_to_next_command(e, n);
        AT_Buffer slice = at_buffer_slice(out, done, n);
        render_block(e, &slice);
        done += n;
    }
    engine_sync_publish(e);                                 // where the sync clock stands, for the control threads
}
