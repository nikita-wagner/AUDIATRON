// ═══════════════════════════════════════════════════════════════════════════
//  dsp_engine.h — the brain: synths → channel strips → master bus + HOOKS
//
//  Signal flow per block (every arrow marked [H] is a hook point where your
//  own code gets the raw float buffer and may do ANYTHING to it in place):
//
//    per mixer channel c:
//      source (synth or custom AT_SourceFn)
//        [H] AT_HOOK_SOURCE        raw source, before channel EQ
//      channel EQ
//        [H] AT_HOOK_CHANNEL       after EQ, before fader/pan
//      fader + pan  ──────────────►  summed into master bus
//
//    master bus:
//        [H] AT_HOOK_MASTER_PRE    sum of all channels, before master EQ
//      master EQ → master fader
//        [H] AT_HOOK_MASTER_POST   before the limiter
//      soft-clip limiter
//        [H] AT_HOOK_OUTPUT        last touch before format conversion → device
//
//  Each hook point has AT_MAX_HOOKS_PER_POINT chained slots, called in slot
//  order. Hooks run ON THE AUDIO THREAD: no malloc, no locks, no printf, no
//  file I/O, no waiting. Keep hook state in memory you own (the `user`
//  pointer) and make sure it outlives the hook registration.
//
//  Threading: every engine_* control function below is safe to call from any
//  thread at any time. It only enqueues a command; the audio thread applies it
//  at the start of its next block (≈ one device period later).
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "at_common.h"
#include "at_queue.h"
#include "synth.h"
#include "equalizer.h"
#include "mixer.h"

typedef enum {
    AT_HOOK_SOURCE,
    AT_HOOK_CHANNEL,
    AT_HOOK_MASTER_PRE,
    AT_HOOK_MASTER_POST,
    AT_HOOK_OUTPUT,
    AT_HOOK_POINT_COUNT
} AT_HookPoint;

// In-place buffer manipulation. buf->frame_clock gives the absolute sample
// position, buf->sample_rate the rate — enough for sample-accurate LFOs etc.
typedef void (*AT_HookFn)(AT_Buffer* buf, void* user);

// Replaces a channel's synth with your own generator (sampler, WAV player,
// line input, ...). The buffer arrives cleared; fill it.
typedef void (*AT_SourceFn)(AT_Buffer* out, void* user);

typedef struct {
    AT_HookFn fn;
    void*     user;
} AT_Hook;

typedef struct {
    AT_SourceFn fn;
    void*       user;
} AT_Source;

typedef struct {
    float     sample_rate;
    uint64_t  frame_clock;
    AT_Queue  queue;
    AT_Synth  synth [AT_MAX_MIX_CHANNELS];
    AT_Source source[AT_MAX_MIX_CHANNELS];
    AT_Mixer  mixer;
    // master points use channel index 0
    AT_Hook   hooks[AT_HOOK_POINT_COUNT][AT_MAX_MIX_CHANNELS][AT_MAX_HOOKS_PER_POINT];
    float     strip_mem[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];
} AT_Engine;

// ~45 KB — allocate statically or on the heap rather than on a thread stack.
void engine_init    (AT_Engine* e, float sample_rate);
void engine_shutdown(AT_Engine* e);

// Matches AT_RenderCallback: pass it to the backend with user = the engine.
// Also callable directly for offline rendering (one rendering thread only).
void engine_render(AT_Buffer* out, void* user);

// ─── Control API (any thread; false = queue full, command dropped) ──────────

bool engine_note_on          (AT_Engine* e, uint32_t channel, int note, float velocity);
bool engine_note_off         (AT_Engine* e, uint32_t channel, int note);
bool engine_all_notes_off    (AT_Engine* e, int channel);           // -1 = every channel
bool engine_set_waveform     (AT_Engine* e, uint32_t channel, AT_Waveform wave);
bool engine_set_adsr         (AT_Engine* e, uint32_t channel, AT_ADSRParams adsr);
bool engine_set_synth_gain   (AT_Engine* e, uint32_t channel, float gain);

bool engine_set_channel_volume(AT_Engine* e, uint32_t channel, float volume);
bool engine_set_channel_pan   (AT_Engine* e, uint32_t channel, float pan);
bool engine_set_channel_mute  (AT_Engine* e, uint32_t channel, bool mute);
bool engine_set_master_volume (AT_Engine* e, float volume);
bool engine_set_soft_clip     (AT_Engine* e, bool enabled);

// channel = -1 addresses the master EQ
bool engine_set_eq_band  (AT_Engine* e, int channel, uint32_t band, AT_FilterType type,
                          float freq, float q, float gain_db);
bool engine_set_eq_bypass(AT_Engine* e, int channel, bool bypass);

// fn = NULL removes the hook/source. For master hook points `channel` is ignored.
bool engine_set_hook  (AT_Engine* e, AT_HookPoint point, uint32_t channel, uint32_t slot,
                       AT_HookFn fn, void* user);
bool engine_set_source(AT_Engine* e, uint32_t channel, AT_SourceFn fn, void* user);

// ─── Metering (any thread, lock-free, approximate) ──────────────────────────

float engine_master_peak (const AT_Engine* e, uint32_t channel);
float engine_channel_peak(const AT_Engine* e, uint32_t channel);
