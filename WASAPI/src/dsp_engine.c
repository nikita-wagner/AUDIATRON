#include "dsp_engine.h"

// ─── Commands ────────────────────────────────────────────────────────────────

enum {
    CMD_NOTE_ON,            // i0 = ch, i1 = note, f0 = velocity
    CMD_NOTE_OFF,           // i0 = ch, i1 = note
    CMD_ALL_NOTES_OFF,      // i0 = ch or -1
    CMD_SET_WAVEFORM,       // i0 = ch, i1 = wave
    CMD_SET_ADSR,           // i0 = ch, f0..f3 = a d s r
    CMD_SET_SYNTH_GAIN,     // i0 = ch, f0 = gain
    CMD_SET_CH_VOLUME,      // i0 = ch, f0 = volume
    CMD_SET_CH_PAN,         // i0 = ch, f0 = pan
    CMD_SET_CH_MUTE,        // i0 = ch, i1 = mute
    CMD_SET_MASTER_VOLUME,  // f0 = volume
    CMD_SET_SOFT_CLIP,      // i0 = enabled
    CMD_SET_EQ_BAND,        // i0 = ch/-1, i1 = band, i2 = type, f0 = freq, f1 = q, f2 = gain_db
    CMD_SET_EQ_BYPASS,      // i0 = ch/-1, i1 = bypass
    CMD_SET_HOOK,           // i0 = point, i1 = ch, i2 = slot, p0 = fn, p1 = user
    CMD_SET_SOURCE          // i0 = ch, p0 = fn, p1 = user
};

static inline bool valid_channel(int ch) { return ch >= 0 && ch < AT_MAX_MIX_CHANNELS; }

static AT_Equalizer* eq_for(AT_Engine* e, int ch) {
    if (ch == -1)         return &e->mixer.master_eq;
    if (valid_channel(ch)) return &e->mixer.ch[ch].eq;
    return NULL;
}

// Audio thread. Everything the command touches is owned by the audio thread,
// so no locks are needed while applying it.
static void apply_command(AT_Engine* e, const AT_Command* c) {
    int ch = c->i[0];

    switch (c->type) {
    case CMD_NOTE_ON:
        if (valid_channel(ch)) synth_note_on(&e->synth[ch], c->i[1], c->f[0]);
        break;
    case CMD_NOTE_OFF:
        if (valid_channel(ch)) synth_note_off(&e->synth[ch], c->i[1]);
        break;
    case CMD_ALL_NOTES_OFF:
        for (int i = 0; i < AT_MAX_MIX_CHANNELS; i++)
            if (ch < 0 || ch == i) synth_all_notes_off(&e->synth[i]);
        break;
    case CMD_SET_WAVEFORM:
        if (valid_channel(ch) && c->i[1] >= 0 && c->i[1] < AT_WAVE_COUNT)
            e->synth[ch].wave = (AT_Waveform)c->i[1];
        break;
    case CMD_SET_ADSR:
        if (valid_channel(ch))
            e->synth[ch].adsr = (AT_ADSRParams){ c->f[0], c->f[1], c->f[2], c->f[3] };
        break;
    case CMD_SET_SYNTH_GAIN:
        if (valid_channel(ch)) e->synth[ch].gain = c->f[0];
        break;
    case CMD_SET_CH_VOLUME:
        if (valid_channel(ch)) e->mixer.ch[ch].volume = at_clampf(c->f[0], 0.0f, 2.0f);
        break;
    case CMD_SET_CH_PAN:
        if (valid_channel(ch)) e->mixer.ch[ch].pan = at_clampf(c->f[0], -1.0f, 1.0f);
        break;
    case CMD_SET_CH_MUTE:
        if (valid_channel(ch)) e->mixer.ch[ch].mute = c->i[1] != 0;
        break;
    case CMD_SET_MASTER_VOLUME:
        e->mixer.master_volume = at_clampf(c->f[0], 0.0f, 2.0f);
        break;
    case CMD_SET_SOFT_CLIP:
        e->mixer.soft_clip = c->i[0] != 0;
        break;
    case CMD_SET_EQ_BAND: {
        AT_Equalizer* eq = eq_for(e, ch);
        if (eq) eq_set_band(eq, (uint32_t)c->i[1], (AT_FilterType)c->i[2], c->f[0], c->f[1], c->f[2]);
        break;
    }
    case CMD_SET_EQ_BYPASS: {
        AT_Equalizer* eq = eq_for(e, ch);
        if (eq) eq->bypass = c->i[1] != 0;
        break;
    }
    case CMD_SET_HOOK: {
        int point = c->i[0], hch = c->i[1], slot = c->i[2];
        if (point < 0 || point >= AT_HOOK_POINT_COUNT) break;
        if (point >= AT_HOOK_MASTER_PRE) hch = 0;
        if (!valid_channel(hch) || slot < 0 || slot >= AT_MAX_HOOKS_PER_POINT) break;
        e->hooks[point][hch][slot] = (AT_Hook){ (AT_HookFn)c->p[0], c->p[1] };
        break;
    }
    case CMD_SET_SOURCE:
        if (valid_channel(ch)) e->source[ch] = (AT_Source){ (AT_SourceFn)c->p[0], c->p[1] };
        break;
    }
}

// ─── Init / shutdown ─────────────────────────────────────────────────────────

void engine_init(AT_Engine* e, float sample_rate) {
    memset(e, 0, sizeof *e);
    e->sample_rate = sample_rate;
    at_queue_init(&e->queue);
    mixer_init(&e->mixer, sample_rate);
    for (int i = 0; i < AT_MAX_MIX_CHANNELS; i++)
        synth_init(&e->synth[i], sample_rate);
}

void engine_shutdown(AT_Engine* e) {
    at_queue_destroy(&e->queue);
}

// ─── Render ──────────────────────────────────────────────────────────────────

static inline void run_hooks(AT_Hook* chain, AT_Buffer* buf) {
    for (int s = 0; s < AT_MAX_HOOKS_PER_POINT; s++)
        if (chain[s].fn) chain[s].fn(buf, chain[s].user);
}

// One block of at most AT_MAX_BLOCK_FRAMES. `bus` is the (stereo) output.
static void render_block(AT_Engine* e, AT_Buffer* bus) {
    const uint32_t frames = bus->frames;
    bus->frame_clock = e->frame_clock;
    at_buffer_clear(bus);

    AT_Buffer strip = {
        .ch          = { e->strip_mem[0], e->strip_mem[1] },
        .channels    = AT_MAX_CHANNELS,
        .frames      = frames,
        .sample_rate = (uint32_t)e->sample_rate,
        .frame_clock = e->frame_clock
    };

    // ── Channel strips ───────────────────────────────────────────────────────
    for (int c = 0; c < AT_MAX_MIX_CHANNELS; c++) {
        if (e->source[c].fn) {
            at_buffer_clear(&strip);
            e->source[c].fn(&strip, e->source[c].user);
        } else {
            synth_render(&e->synth[c], strip.ch[0], frames);
            memcpy(strip.ch[1], strip.ch[0], frames * sizeof(float));
        }

        run_hooks(e->hooks[AT_HOOK_SOURCE][c], &strip);
        eq_process(&e->mixer.ch[c].eq, &strip);
        run_hooks(e->hooks[AT_HOOK_CHANNEL][c], &strip);
        mixer_channel_to_bus(&e->mixer, (uint32_t)c, &strip, bus);
    }

    // ── Master bus ───────────────────────────────────────────────────────────
    run_hooks(e->hooks[AT_HOOK_MASTER_PRE][0], bus);
    eq_process(&e->mixer.master_eq, bus);
    mixer_master_gain(&e->mixer, bus);
    run_hooks(e->hooks[AT_HOOK_MASTER_POST][0], bus);
    mixer_master_limit(&e->mixer, bus);
    run_hooks(e->hooks[AT_HOOK_OUTPUT][0], bus);

    e->frame_clock += frames;
}

void engine_render(AT_Buffer* out, void* user) {
    AT_Engine* e = (AT_Engine*)user;

    // Commands land at block granularity (one device period, ~3–10 ms).
    // TODO (sample-accurate events): add a frame timestamp to AT_Command and
    // split the block at each event's offset.
    AT_Command cmd;
    while (at_queue_pop(&e->queue, &cmd))
        apply_command(e, &cmd);

    for (uint32_t done = 0; done < out->frames; ) {
        uint32_t  n     = out->frames - done;
        if (n > AT_MAX_BLOCK_FRAMES) n = AT_MAX_BLOCK_FRAMES;
        AT_Buffer slice = at_buffer_slice(out, done, n);
        render_block(e, &slice);
        done += n;
    }
}

// ─── Control API ─────────────────────────────────────────────────────────────

static bool push(AT_Engine* e, AT_Command c) { return at_queue_push(&e->queue, &c); }

bool engine_note_on(AT_Engine* e, uint32_t channel, int note, float velocity) {
    return push(e, (AT_Command){ .type = CMD_NOTE_ON, .i = { (int)channel, note }, .f = { velocity } });
}

bool engine_note_off(AT_Engine* e, uint32_t channel, int note) {
    return push(e, (AT_Command){ .type = CMD_NOTE_OFF, .i = { (int)channel, note } });
}

bool engine_all_notes_off(AT_Engine* e, int channel) {
    return push(e, (AT_Command){ .type = CMD_ALL_NOTES_OFF, .i = { channel } });
}

bool engine_set_waveform(AT_Engine* e, uint32_t channel, AT_Waveform wave) {
    return push(e, (AT_Command){ .type = CMD_SET_WAVEFORM, .i = { (int)channel, (int)wave } });
}

bool engine_set_adsr(AT_Engine* e, uint32_t channel, AT_ADSRParams a) {
    return push(e, (AT_Command){ .type = CMD_SET_ADSR, .i = { (int)channel },
                                 .f = { a.attack_s, a.decay_s, a.sustain, a.release_s } });
}

bool engine_set_synth_gain(AT_Engine* e, uint32_t channel, float gain) {
    return push(e, (AT_Command){ .type = CMD_SET_SYNTH_GAIN, .i = { (int)channel }, .f = { gain } });
}

bool engine_set_channel_volume(AT_Engine* e, uint32_t channel, float volume) {
    return push(e, (AT_Command){ .type = CMD_SET_CH_VOLUME, .i = { (int)channel }, .f = { volume } });
}

bool engine_set_channel_pan(AT_Engine* e, uint32_t channel, float pan) {
    return push(e, (AT_Command){ .type = CMD_SET_CH_PAN, .i = { (int)channel }, .f = { pan } });
}

bool engine_set_channel_mute(AT_Engine* e, uint32_t channel, bool mute) {
    return push(e, (AT_Command){ .type = CMD_SET_CH_MUTE, .i = { (int)channel, mute } });
}

bool engine_set_master_volume(AT_Engine* e, float volume) {
    return push(e, (AT_Command){ .type = CMD_SET_MASTER_VOLUME, .f = { volume } });
}

bool engine_set_soft_clip(AT_Engine* e, bool enabled) {
    return push(e, (AT_Command){ .type = CMD_SET_SOFT_CLIP, .i = { enabled } });
}

bool engine_set_eq_band(AT_Engine* e, int channel, uint32_t band, AT_FilterType type,
                        float freq, float q, float gain_db) {
    return push(e, (AT_Command){ .type = CMD_SET_EQ_BAND, .i = { channel, (int)band, (int)type },
                                 .f = { freq, q, gain_db } });
}

bool engine_set_eq_bypass(AT_Engine* e, int channel, bool bypass) {
    return push(e, (AT_Command){ .type = CMD_SET_EQ_BYPASS, .i = { channel, bypass } });
}

bool engine_set_hook(AT_Engine* e, AT_HookPoint point, uint32_t channel, uint32_t slot,
                     AT_HookFn fn, void* user) {
    return push(e, (AT_Command){ .type = CMD_SET_HOOK, .i = { (int)point, (int)channel, (int)slot },
                                 .p = { (void*)fn, user } });
}

bool engine_set_source(AT_Engine* e, uint32_t channel, AT_SourceFn fn, void* user) {
    return push(e, (AT_Command){ .type = CMD_SET_SOURCE, .i = { (int)channel },
                                 .p = { (void*)fn, user } });
}

// ─── Metering ────────────────────────────────────────────────────────────────

float engine_master_peak(const AT_Engine* e, uint32_t channel) {
    return channel < AT_MAX_CHANNELS ? e->mixer.master_peak[channel] : 0.0f;
}

float engine_channel_peak(const AT_Engine* e, uint32_t channel) {
    return channel < AT_MAX_MIX_CHANNELS ? e->mixer.ch[channel].peak : 0.0f;
}
