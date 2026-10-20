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
//      fader + pan  ──────────────►  summed into the master bus, or into its bus
//
//    buses (only those a channel is sent to, engine_set_channel_bus):
//      sum of their channels → bus EQ → [H] AT_HOOK_BUS → bus volume / mute → bus meter
//                   ──────────────►  summed into the master bus
//      A bus is a group of channels with its own EQ and fader, e.g. "everything
//      above this master track". A channel on no bus (the default) goes to the
//      master bus directly.
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
//    engine_sync.c       one clock for the channels switched to sync
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

#define AT_ENGINE_CHANNELS    32    // mixer channels (source + EQ + strip each)
#define AT_ENGINE_NOTES       512   // notes the engine tells apart: MIDI's 0..127 and above (a drumpad's upper pads)
#define AT_ENGINE_HOOK_SLOTS  4     // chained effects per hook point
#define AT_ENGINE_TIMED       2048  // commands waiting for their frame (engine_at)
#define AT_ENGINE_BUSES       8     // groups of channels with their own EQ and fader
#define AT_EQ_BUS(b)          (-2 - (b))    // the `channel` of a bus's EQ in engine_set_eq_band / _bypass

typedef enum {
    AT_HOOK_SOURCE,
    AT_HOOK_CHANNEL,
    AT_HOOK_BUS,                    // a bus after its EQ, before its fader (`channel` = the bus)
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
    uint64_t        clock_offset;   // this channel's source and effects see frame_clock - clock_offset
    int             bus;            // the bus it is summed into, -1 = the master bus directly
} AT_EngineChannel;

typedef struct {
    AT_Equalizer    eq;
    float           volume;         // 0..2, 1 = unchanged
    float           gain;           // what is applied now: follows volume (and mute) without clicks
    bool            mute;
    AT_PeakMeter    meter;
} AT_EngineBus;

// What a control thread asked the engine to do, as seen by an observer
// (engine_set_observer). The fields that do not apply to a type are 0.
typedef enum {
    AT_ENGINE_EV_NOTE_ON,           // channel, note, value = velocity
    AT_ENGINE_EV_NOTE_OFF,          // channel, note
    AT_ENGINE_EV_ALL_NOTES_OFF,     // channel (-1 = every channel)
    AT_ENGINE_EV_PARAM              // fn, self, param, value
} AT_EngineEventType;

typedef struct {
    AT_EngineEventType type;
    int         channel;
    int         note;
    float       value;
    AT_ParamFn  fn;
    void*       self;
    int         param;
} AT_EngineEvent;

typedef void (*AT_EngineObserver)(void* user, const AT_EngineEvent* ev);

// The sync clock (engine_sync.c). Audio thread, except where it says otherwise.
typedef struct {
    bool              on[AT_ENGINE_CHANNELS];       // the channel is on the clock
    uint64_t          origin;                       // engine frame of the clock's time 0
    bool              fixed;                        // pinned from outside (a transport): runs, keeps its origin
    double            step;                         // frames between grid points for notes (0 = they play at once)
    double            bar;                          // frames per bar (0 = unknown)
    uint64_t          note_at[AT_ENGINE_CHANNELS][AT_ENGINE_NOTES];   // where a held-back note-on landed
    // control side: as last sent
    bool              sent_on[AT_ENGINE_CHANNELS];
    double            sent_step, sent_bar;
    // published after every block for the control threads (read with engine_sync_clock)
    volatile uint32_t shown_seq;
    volatile uint32_t shown_origin_lo, shown_origin_hi;
    volatile int32_t  shown_running;
} AT_EngineSync;

typedef struct {
    float            sample_rate;
    uint64_t         frame_clock;
    AT_Queue         queue;

    // control side: the time and tag given to the next commands (engine_at),
    // and who is told about notes and parameters (engine_set_observer)
    uint64_t         stamp_frame;
    int32_t          stamp_tag;
    AT_EngineObserver observer;
    void*            observer_user;

    // audio side: timed commands waiting for their frame, in frame order
    AT_Command       timed[AT_ENGINE_TIMED];
    uint32_t         timed_count;

    AT_EngineChannel channel[AT_ENGINE_CHANNELS];
    AT_EngineBus     bus[AT_ENGINE_BUSES];
    float            bus_mem[AT_ENGINE_BUSES][AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];

    AT_Equalizer     master_eq;
    AT_MasterFader   master_fader;
    AT_SoftClip      limiter;
    bool             limiter_enabled;
    AT_PeakMeter     master_meter;

    // master hook points use channel index 0
    AT_Hook          hooks[AT_HOOK_POINT_COUNT][AT_ENGINE_CHANNELS][AT_ENGINE_HOOK_SLOTS];

    float            strip_mem[AT_MAX_CHANNELS][AT_MAX_BLOCK_FRAMES];

    AT_EngineSync    sync;
} AT_Engine;

// ~300 KB — allocate statically or on the heap rather than on a thread stack.
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
// The time this channel's source and effects see is frame_clock - offset
// (default 0). Things locked to the clock, like a tremolo's LFO, then run as
// if the channel were `offset` frames earlier: a replay uses it so a copy's
// LFOs are in the same phase as when the take was recorded.
bool engine_set_channel_clock (AT_Engine* e, uint32_t channel, uint64_t offset);
bool engine_set_master_volume (AT_Engine* e, float volume);
// Buses: bus = -1 sends the channel to the master bus directly (the default).
bool engine_set_channel_bus   (AT_Engine* e, uint32_t channel, int bus);
bool engine_set_bus_volume    (AT_Engine* e, uint32_t bus, float volume);
bool engine_set_bus_mute      (AT_Engine* e, uint32_t bus, bool mute);
bool engine_set_limiter       (AT_Engine* e, bool enabled);

// Equalizers. channel = -1 addresses the master EQ, AT_EQ_BUS(b) the EQ of bus b.
bool engine_set_eq_band  (AT_Engine* e, int channel, uint32_t band, AT_FilterType type,
                          float freq, float q, float gain_db);
bool engine_set_eq_bypass(AT_Engine* e, int channel, bool bypass);

// ─── Timing: commands on an exact frame ─────────────────────────────────────
//
// Every command sent after engine_at(e, frame, tag) is applied exactly at
// engine frame `frame` (the audio block is split there), instead of at the
// start of the next block. engine_at(e, 0, 0) goes back to "as soon as
// possible". A frame that has already passed is applied at once.
//
//     engine_at(&engine, start + 24012, 1);       // 0.50025 s after `start`
//     engine_note_on(&engine, 0, 60, 0.9f);
//     engine_at(&engine, 0, 0);
//
// The setting belongs to the calling control thread: use it from one thread,
// or reset it before another thread sends. `tag` (not 0) marks the commands so
// engine_cancel can drop those still waiting.
void engine_at    (AT_Engine* e, uint64_t frame, int tag);
// The engine frame the next block starts at (frames rendered so far); any
// thread. For timing commands with engine_at.
uint64_t engine_now(const AT_Engine* e);
bool engine_cancel(AT_Engine* e, int tag);          // drops waiting commands with this tag (applies at once)

// ─── Sync: channels on one clock (engine_sync.c) ────────────────────────────
//
// Channels switched to sync share one clock, so what they play starts from
// the same position in time:
//   loops   a loop starting on a synced channel (drumpad loop, player song,
//           machine) joins the clock in phase: it starts where it would be
//           had it started with the clock. While nothing synced plays the
//           clock is idle, and the next synced loop starts it from its top.
//   notes   while the clock runs, notes and hits sent to a synced channel
//           wait for its next grid step (step 0 = they play at once); a
//           note-off lands at least one step after its note-on. Notes sent
//           with engine_at keep their frame.
//   origin  a transport can pin the clock to its beat 0 (fixed): it then runs
//           while the transport runs.
bool engine_set_channel_sync(AT_Engine* e, uint32_t channel, bool on);
bool engine_channel_sync_sent(const AT_Engine* e, uint32_t channel);    // control view: as last set
bool engine_set_sync_grid   (AT_Engine* e, double step_frames, double bar_frames);   // 0 = none
bool engine_set_sync_origin (AT_Engine* e, uint64_t origin, bool fixed);             // fixed = false: free again
// Control thread: does the clock run, and its origin (as of the last block).
bool     engine_sync_clock   (const AT_Engine* e, uint64_t* origin);
// Control thread: the clock's next bar line at or after `frame` (frame itself when it is idle or has no bars).
uint64_t engine_sync_next_bar(const AT_Engine* e, uint64_t frame);
// Audio thread (sources, parameter functions):
bool     engine_channel_synced(const AT_Engine* e, uint32_t channel);
double   engine_sync_join     (AT_Engine* e, uint32_t channel);   // frames since the clock's start (0: this starts it)
uint64_t engine_sync_origin   (const AT_Engine* e);

// ─── Observing (control thread) ─────────────────────────────────────────────
//
// fn is called for every note-on, note-off, all-notes-off and parameter
// command, on the thread that sends it, before it is queued. For recording
// what instruments do. NULL stops it.
void engine_set_observer(AT_Engine* e, AT_EngineObserver fn, void* user);

// ─── Metering (any thread, lock-free, approximate) ──────────────────────────

float engine_master_peak (const AT_Engine* e, uint32_t channel);    // 0 = left, 1 = right
float engine_channel_peak(const AT_Engine* e, uint32_t channel);
float engine_bus_peak    (const AT_Engine* e, uint32_t bus, uint32_t channel);
