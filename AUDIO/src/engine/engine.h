// ═══════════════════════════════════════════════════════════════════════════
//  engine/engine.h — the signal graph: sources → channel strips → master bus
//
//  The engine is pure wiring. It owns no synth and no effect; it only knows
//  the contracts in core/at_module.h. You plug things into it:
//
//    engine_set_source()   a sound generator into a channel
//    engine_set_hook()     an effect into one of the hook points [H]
//
//  Signal flow per block:
//
//    per channel (only channels that have a source):
//      source.render
//        [H] AT_HOOK_SOURCE        raw source, before the channel EQ
//      channel EQ
//        [H] AT_HOOK_CHANNEL       after the EQ, before fader/pan
//      fader + pan  ──────────────►  summed into the master bus
//
//    master bus:
//        [H] AT_HOOK_MASTER_PRE    sum of all channels, before the master EQ
//      master EQ → master fader
//        [H] AT_HOOK_MASTER_POST   before the limiter
//      limiter (soft clip) → master meter
//        [H] AT_HOOK_OUTPUT        last touch before format conversion → device
//
//  Each hook point has AT_ENGINE_HOOK_SLOTS chained slots, run in slot order.
//
//  Files in this folder:
//    engine_init.c       setup / teardown
//    engine_render.c     the graph above                    (audio thread)
//    engine_commands.c   applies queued commands            (audio thread)
//    engine_control.c    the engine_* functions below       (control threads)
//
//  Threading: every engine_* control function is safe to call from any thread
//  at any time. It only enqueues a command; the audio thread applies it at the
//  start of its next block (≈ one device period later). State you hand to the
//  engine (a synth, an effect struct) must outlive its registration.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"
#include "core/at_module.h"
#include "core/at_queue.h"
#include "equalizer/equalizer.h"
#include "mixer/channel_strip/channel_strip.h"
#include "mixer/master_fader/master_fader.h"
#include "mixer/peak_meter/peak_meter.h"
#include "effects/soft_clip/soft_clip.h"

#define AT_ENGINE_CHANNELS    8     // mixer channels (source + EQ + strip each)
#define AT_ENGINE_HOOK_SLOTS  4     // chained effects per hook point

typedef enum {
    AT_HOOK_SOURCE,
    AT_HOOK_CHANNEL,
    AT_HOOK_MASTER_PRE,
    AT_HOOK_MASTER_POST,
    AT_HOOK_OUTPUT,
    AT_HOOK_POINT_COUNT
} AT_HookPoint;

typedef struct {
    AT_ProcessFn fn;                // NULL = empty slot
    void*        user;
} AT_Hook;

typedef struct {
    const AT_SourceType* type;      // NULL = empty channel (skipped entirely)
    void*                self;
} AT_Source;

typedef struct {
    AT_Source       source;
    bool            hold;           // latched: note-offs are ignored until the hold is released
    AT_Equalizer    eq;
    AT_ChannelStrip strip;
} AT_EngineChannel;

typedef struct {
    float            sample_rate;
    uint64_t         frame_clock;
    AT_Queue         queue;

    AT_EngineChannel channel[AT_ENGINE_CHANNELS];

    AT_Equalizer     master_eq;
    AT_MasterFader   master_fader;
    AT_SoftClip      limiter;
    bool             limiter_enabled;
    AT_PeakMeter     master_meter;

    // master hook points use channel index 0
    AT_Hook          hooks[AT_HOOK_POINT_COUNT][AT_ENGINE_CHANNELS][AT_ENGINE_HOOK_SLOTS];

    float            strip_mem[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];
} AT_Engine;

// ~30 KB — allocate statically or on the heap rather than on a thread stack.
void engine_init    (AT_Engine* e, float sample_rate);
void engine_shutdown(AT_Engine* e);

// AT_ProcessFn: fills `out` (stereo) with the next out->frames frames; user =
// the engine. Hand it to the audio thread, or call it yourself for offline
// rendering (only one thread may render).
void engine_render(AT_Buffer* out, void* user);

// ─── Control API (any thread; false = queue full, command dropped) ──────────

// Sources. type = NULL empties the channel.
bool engine_set_source   (AT_Engine* e, uint32_t channel, const AT_SourceType* type, void* self);
bool engine_note_on      (AT_Engine* e, uint32_t channel, int note, float velocity);
bool engine_note_off     (AT_Engine* e, uint32_t channel, int note);
bool engine_all_notes_off(AT_Engine* e, int channel);               // -1 = every channel

// Latch a channel: while it is held, note-offs are ignored, so its notes keep
// sounding after their keys are released. Releasing the hold stops every note
// on the channel. engine_all_notes_off still works while a channel is held.
bool engine_set_channel_hold(AT_Engine* e, uint32_t channel, bool hold);

// Effects. fn = NULL removes the hook. For master hook points `channel` is ignored.
bool engine_set_hook(AT_Engine* e, AT_HookPoint point, uint32_t channel, uint32_t slot,
                     AT_ProcessFn fn, void* user);

// Runs fn(self, param, value) on the audio thread between two blocks — the
// safe way to change a parameter of any plugged-in source or effect.
bool engine_set_param(AT_Engine* e, AT_ParamFn fn, void* self, int param, float value);

// Mixer.
bool engine_set_channel_volume(AT_Engine* e, uint32_t channel, float volume);
bool engine_set_channel_pan   (AT_Engine* e, uint32_t channel, float pan);
bool engine_set_channel_mute  (AT_Engine* e, uint32_t channel, bool mute);
bool engine_set_master_volume (AT_Engine* e, float volume);
bool engine_set_limiter       (AT_Engine* e, bool enabled);

// Equalizers. channel = -1 addresses the master EQ.
bool engine_set_eq_band  (AT_Engine* e, int channel, uint32_t band, AT_FilterType type,
                          float freq, float q, float gain_db);
bool engine_set_eq_bypass(AT_Engine* e, int channel, bool bypass);

// ─── Metering (any thread, lock-free, approximate) ──────────────────────────

float engine_master_peak (const AT_Engine* e, uint32_t channel);    // 0 = left, 1 = right
float engine_channel_peak(const AT_Engine* e, uint32_t channel);
