// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad.h — 16 pads, one WAV / MP3 sound each
//
//  Every pad is one key and one sound file. Each pad has its OWN pitch, loop,
//  volume, pan and pitch mode, so a kick can sit 3 semitones down while a loop
//  next to it runs at its original pitch.
//
//      pads   A S D F G H J K        (pads 1..8)
//             Q W E R T Z U I        (pads 9..16, QWERTZ labels)
//
//  Hold a pad and press a control key to change THAT pad (hold several pads to
//  change them together). A control key with no pad held does nothing, so a
//  change can never land on a pad you did not mean. To change a running loop,
//  hold its key: a press used for a change does not stop the loop.
//
//      + / -           pitch +1 / -1 semitone (also the numpad + / -)
//      Up / Down , .   volume of the pad      Left / Right   pan
//      L               loop on / off          P   tape / keep-tempo pitch
//      C  V  F1        crush / tremolo / echo on this pad only
//      X               sync: on the shared clock / free (async)
//      /               timing: type four values in ms, Enter after each
//                      (empty Enter keeps the value; Esc cancels):
//                        start, end   the part of the file (empty end = to the end)
//                        wait         silence after the press before it plays
//                        play for     how long it sounds (0 = the part's length);
//                                     with loop: it repeats every this long
//                      e.g. 250 1000 2000 1000: 2 s after the press, plays
//                      250..1000 ms of its file for 1 s (the 750 ms part, then
//                      250 ms of silence)
//      Backspace       reset pitch, volume, pan, effects
//
//  *  the pattern (a step sequencer; no pad needs to be held): a grid of bits,
//     one row per pad (row 1 = pad 1 ...), one column per step. A 1 plays the
//     row's pad on that step, a 0 does not; every step lasts the step time;
//     the pattern loops. First "*" asks the size ("4x4" = pads 1-4, 4 steps),
//     then the step time ("500" ms or "0.5s"), then every row as 0 / 1. Once
//     a pattern exists, "*" asks only the step time (type "8x4" there to
//     resize) and the rows. Finishing starts it; step time 0 or Space stops it.
//
//  On the drumpad track these keys never act on the whole track: `, .` and
//  `C V F1` are the track volume and the track's effects on the other tracks,
//  here they belong to the pads.
//
//  Every pad is its own layer: it renders into its own buffer, runs its own
//  pitch shifter and effects there, and only the finished result is added to
//  the shared output buffer. Nothing of one pad's processing reaches another.
//
//  The clock. Pads are SYNCED (default) or FREE, each on its own:
//    synced  the pad runs on the drumpad's one shared clock. The first synced
//            loop that starts (while no synced loop is playing) starts the
//            clock. A synced loop started later does not start from its
//            beginning: it drops in at the position it would have reached had
//            it started together with the clock, so all synced loops stay
//            phase-locked to the sample, however late they are pressed. Their
//            tremolo LFOs run on the same clock too.
//    free    the pad has its own clock that starts when the pad starts: a loop
//            begins at its beginning the moment it is pressed, and its tremolo
//            starts its cycle there.
//  So pads 1 and 3 can be locked together while pad 2 runs free (hold S,
//  press X). One-shot hits always play the moment they are pressed. A synced
//  loop pitched in tape mode plays faster or slower and so leaves the clock's
//  phase; keep-tempo pitch (P) keeps it locked.
//
//  Part of a file: drumpad_set_cut_ms(d, pad, 250, 1000) plays a pad from
//  250 ms to 1000 ms of its file (one-shot and loop), frame-exact, so pads cut
//  to the same length loop in step; drumpad_set_cut(d, pad, 0.2f, 0.9f) does
//  it in fractions of the file. On the command line:
//  --pad "kick.mp3(250ms,1000ms)", --pad "kick.mp3(0.5)" (the first half).
//
//  One-shot pad (default): a press plays the sound to its end; pressing again
//  restarts it. Looped pad: a press starts the loop, the next press stops it.
//  A press used to change the pad (hold A, press +) does not stop its loop.
//  Holding a pad and pressing L starts the loop right away; L again lets the
//  loop finish its current pass and stop. Space (the app's all-notes-off)
//  stops every loop.
//
//  Pitch modes, per pad:
//    tape          pitch and speed move together, like a record (default;
//                  exact and clean, a pitched-up loop also gets faster)
//    keep tempo    the pitch changes, the speed stays (effects/pitch_shift;
//                  slight flutter, transients get a little softer)
//
//  How it is built: every loaded pad is one sampler (sampler/) with its own
//  pitch shifter, and all pads are the layers of one layer stack (layers/),
//  each layer answering only its pad's note (36 + pad index, the General MIDI
//  drum notes start there). So engine_note_on(engine, channel, 36, v) hits
//  pad 1 from code or, later, from a MIDI pad controller.
//
//  Files in this folder:
//    drumpad.c           load files, build the layer stack, AT_Instrument
//    drumpad_keys.c      what each key does
//    drumpad_control.c   per-pad settings and hits, sent to the audio thread
//    drumpad_effects.c   the effects every pad owns (its second layer insert)
//    drumpad_clock.c     the shared clock and the engine source (the stack + clock)
//    drumpad_cut_edit.c  "/": typing a pad's cut in milliseconds
//    drumpad_pattern.c   the pattern (step sequencer): sending its hits on time
//    drumpad_pattern_edit.c  "*": typing the pattern
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "effects/bitcrusher/bitcrusher.h"
#include "effects/echo/echo.h"
#include "effects/pitch_shift/pitch_shift.h"
#include "effects/tremolo/tremolo.h"
#include "instruments/instrument.h"
#include "io/audio_clip/audio_clip.h"
#include "layers/layer_stack.h"
#include "sampler/sampler.h"

#define AT_DRUMPAD_PADS         16
#define AT_DRUMPAD_FIRST_NOTE   36          // note of pad 0 (General MIDI: bass drum)
#define AT_DRUMPAD_PITCH_MIN    (-24.0f)    // semitones
#define AT_DRUMPAD_PITCH_MAX    ( 24.0f)
#define AT_DRUMPAD_VOLUME_MAX   2.0f

_Static_assert(AT_DRUMPAD_PADS <= AT_LAYER_MAX, "every pad needs a layer");
_Static_assert(AT_LAYER_INSERTS >= 2, "a pad uses two inserts: pitch shifter + its effects");

// The effects every pad has for itself, run in this order.
typedef enum {
    AT_PAD_FX_CRUSH,
    AT_PAD_FX_TREMOLO,
    AT_PAD_FX_ECHO,
    AT_PAD_FX_COUNT
} AT_PadFx;

// Everything that can happen to a pad, as data: what a recording stores and a
// replay plays back (drumpad_events.c). Every drumpad_* control function below
// reports exactly one of these to the observer.
typedef enum {
    AT_PAD_EV_HIT,                          // a = velocity
    AT_PAD_EV_LOOP_START,
    AT_PAD_EV_LOOP_STOP,
    AT_PAD_EV_LOOP,                         // a = 1 on / 0 off (the setting)
    AT_PAD_EV_PITCH,                        // a = semitones
    AT_PAD_EV_KEEP_TEMPO,                   // a = 1 / 0
    AT_PAD_EV_VOLUME,                       // a
    AT_PAD_EV_PAN,                          // a
    AT_PAD_EV_FX,                           // fx = AT_PadFx, a = 1 / 0
    AT_PAD_EV_SYNC,                         // a = 1 synced / 0 free
    AT_PAD_EV_CUT,                          // a = from, b = to (fractions of the file)
    AT_PAD_EV_CUT_MS,                       // a = from, b = to (milliseconds)
    AT_PAD_EV_ECHO_MS,                      // a = time between echo repeats, ms
    AT_PAD_EV_TIMING,                       // a = wait, b = play for (ms; 0 = none)
    AT_PAD_EV_RESET
} AT_PadEventType;

typedef struct {
    AT_PadEventType type;
    int             pad;                    // 0..AT_DRUMPAD_PADS-1
    int             fx;                     // AT_PadFx, for AT_PAD_EV_FX
    float           a, b;
} AT_PadEvent;

// Called on the control thread after every change made through drumpad_*.
typedef void (*AT_PadObserver)(void* user, const AT_PadEvent* ev);

// The pattern: a step sequencer over the pads (drumpad_pattern.c).
#define AT_PATTERN_MAX_STEPS   32           // a row's steps are the bits of a uint32_t

typedef struct {
    int      rows;                          // pads 1..rows; 0 = no pattern yet
    int      steps;
    uint32_t bits[AT_DRUMPAD_PADS];         // row r: bit s set = pad r plays on step s
    float    step_ms;                       // how long one step lasts

    // playing, control thread: hits are sent ahead with engine_at
    bool     playing;
    uint64_t start;                         // engine frame of step 0
    uint64_t next;                          // the next step to send (counting on through the passes)
} AT_DrumPattern;

// The shared clock, in engine frames (AT_Buffer.frame_clock). Audio thread only.
typedef struct {
    uint64_t next;                          // frame_clock of the block about to be rendered
    uint64_t origin;                        // when the clock last started
} AT_DrumpadClock;

typedef struct {
    bool          loaded;
    AT_Clip       clip;                     // the decoded file (owned, unless shared)
    bool          shared;                   // the clip's samples belong to another drumpad (a copy's pads)
    AT_Sampler    sampler;                  // the pad's sound (a layer of the stack)
    AT_PitchShift shifter;                  // layer insert 0; idle unless the pad is in keep-tempo mode
    int           layer;                    // index in the stack, -1 = not in it (yet)
    char          label[32];                // file name without the folder
    char          path[260];                // the file as it was loaded (a recording names it)

    // the pad's own effects (layer insert 1, drumpad_effects.c)
    AT_Bitcrusher crusher;
    AT_Tremolo    tremolo;
    void*         echo_memory;              // ~1 MB, allocated the first time echo is switched on (control thread owns it)
    AT_Echo*      echo;                     // audio thread: inside echo_memory once it has been handed over
    bool          fx_running[AT_PAD_FX_COUNT];  // audio thread
    int           echo_ms;                  // the pad's echo: time between repeats (control view)

    // timing, audio thread (drumpad_clock.c)
    const AT_DrumpadClock* clock;           // the drumpad's shared clock
    bool          synced;                   // on the shared clock (else on its own)
    uint64_t      own_origin;               // when this pad last started: its own clock

    // settings, control-thread view
    float         pitch;                    // semitones
    float         volume;                   // 0..2
    float         pan;                      // -1..+1
    bool          loop;
    bool          keep_tempo;
    bool          fx[AT_PAD_FX_COUNT];
    bool          sync;                     // true = on the shared clock (default), false = free
    float         cut_from;                 // part of the file that plays, 0..1 of its length
    float         cut_to;
    bool          cut_in_ms;                // the cut was set in milliseconds (exact): these count
    float         cut_from_ms;
    float         cut_to_ms;
    float         wait_ms;                  // silence after a press before it plays (0 = none)
    float         play_ms;                  // how long it sounds / repeats (0 = the part's own length)

    // key state, control thread only
    bool          held;
    bool          changed_while_held;       // a control key was used during this press
    bool          stop_on_release;          // this press found the loop running
} AT_DrumPad;

typedef struct {
    AT_DrumPad    pads[AT_DRUMPAD_PADS];
    AT_LayerStack stack;                    // one layer per loaded pad, rendered through AT_DRUMPAD_SOURCE
    AT_DrumpadClock clock;

    AT_Engine*    engine;                   // NULL until attach: settings are then written directly
    uint32_t      channel;
    bool          prepared;                 // the layer stack is built
    int           selected;                 // the pad hit last (shown in the status line), -1 = none

    // the "/" cut editor (drumpad_keys.c), control thread
    int           edit_pad;                 // -1 = not editing
    int           edit_field;               // 0 = start, 1 = end, 2 = wait, 3 = play for
    char          edit_text[8];             // the digits typed so far
    float         edit_from_ms;             // the values entered so far
    float         edit_to_ms;
    float         edit_wait_ms;
    char          edit_note[48];            // why the last Enter was refused

    AT_DrumPattern pattern;
    // the "*" pattern editor (drumpad_pattern_edit.c), control thread
    int           pat_stage;                // -1 = closed, 0 = size, 1 = step time, 2.. = row stage-2
    char          pat_text[40];
    char          pat_note[48];
    AT_DrumPattern pat_draft;               // what is being typed; becomes the pattern on the last Enter

    AT_PadObserver observer;                // NULL = nobody listens
    void*         observer_user;
    int           quiet;                    // > 0: changes are not reported (inside drumpad_reset)
    float         sample_rate;
    char          help[1024];               // built at attach: key labels of this layout + loaded files
} AT_Drumpad;

// ─── drumpad.c ───────────────────────────────────────────────────────────────

void drumpad_init(AT_Drumpad* d, float sample_rate);

// Loads a file onto a pad (0..AT_DRUMPAD_PADS-1). Only before attach.
// false = failed, `error` says why.
bool drumpad_load(AT_Drumpad* d, int pad, const char* path, char* error, size_t error_cap);
void drumpad_free(AT_Drumpad* d);           // frees every loaded file
// A copy that plays the same files (sharing them, not loading them again) with
// its own settings: recordings are replayed on it (app_replay.c). The original
// must outlive it and stay loaded; both before attach.
void drumpad_init_copy(AT_Drumpad* copy, const AT_Drumpad* original, float sample_rate);

int  drumpad_loaded_count(const AT_Drumpad* d);

// Builds the layer stack of the loaded pads (attach does it too). A drumpad
// that is never attached, like the replay's, is then played by rendering
// AT_DRUMPAD_SOURCE yourself; with no engine every drumpad_* call acts directly,
// so call them from the thread that renders it.
void drumpad_prepare(AT_Drumpad* d);

// Describes the drumpad to the app. `d` must outlive the returned struct's use.
AT_Instrument drumpad_instrument(AT_Drumpad* d);

// ─── drumpad_control.c ───────────────────────────────────────────────────────
// For keys and for code (a game, a sequencer). Before attach these write the
// settings directly; afterwards they go through the engine's command queue.
// Unloaded pads and bad indices are ignored; values are clamped.

void drumpad_hit           (AT_Drumpad* d, int pad, float velocity);    // one-shot
void drumpad_loop_start    (AT_Drumpad* d, int pad);                    // also turns loop on
void drumpad_loop_stop     (AT_Drumpad* d, int pad);                    // now, with a short fade
bool drumpad_is_looping    (const AT_Drumpad* d, int pad);              // approximate (one block late)

void drumpad_set_pitch     (AT_Drumpad* d, int pad, float semitones);
void drumpad_set_keep_tempo(AT_Drumpad* d, int pad, bool keep_tempo);
void drumpad_set_loop      (AT_Drumpad* d, int pad, bool loop);         // off: a running loop finishes its pass
void drumpad_set_volume    (AT_Drumpad* d, int pad, float volume);
void drumpad_set_pan       (AT_Drumpad* d, int pad, float pan);
void drumpad_reset         (AT_Drumpad* d, int pad);                    // pitch 0, tape, volume 1, centre, effects off
void drumpad_set_sync      (AT_Drumpad* d, int pad, bool sync);         // true = shared clock, false = free

// Plays only from..to of the pad's file, as fractions of its length:
// (0, 0.5) = the first half, (0.2, 0.9) = from 20 % to 90 %, (0, 1) = all.
// false = from / to out of order or outside 0..1 (nothing changes).
bool drumpad_set_cut       (AT_Drumpad* d, int pad, float from, float to);
// The same in milliseconds of the file, frame-exact: (250, 1000) plays 750 ms.
// to_ms past the end = to the end. false = start not inside the file, or end
// not after start.
bool drumpad_set_cut_ms    (AT_Drumpad* d, int pad, float from_ms, float to_ms);
double drumpad_length_ms   (const AT_Drumpad* d, int pad);              // of the pad's whole file, 0 = no file

// When and how long the pad sounds, in ms of real time: it waits `wait_ms`
// after a press (or loop start), then plays for `play_ms` (shorter than its
// part = cut off with a 5 ms fade, longer = silence after the part; 0 = the
// part's own length). A looped pad repeats every `play_ms`. Both 0 = play
// at once, as long as the part is. false = a negative value.
//     drumpad_set_cut_ms(d, pad, 250, 1000); drumpad_set_timing(d, pad, 2000, 1000);
bool drumpad_set_timing    (AT_Drumpad* d, int pad, float wait_ms, float play_ms);

// Applies every setting of a pad that has just been plugged in (drumpad.c).
void drumpad_apply_all     (AT_Drumpad* d, int pad);

// ─── drumpad_effects.c ───────────────────────────────────────────────────────

void drumpad_fx_init   (AT_DrumPad* p, float sample_rate);              // at load
void drumpad_fx_free   (AT_DrumPad* p);                                 // audio stopped
// Switches one effect of one pad. false = no memory for the echo (stays off).
bool drumpad_set_fx    (AT_Drumpad* d, int pad, AT_PadFx fx, bool on);
// The time between the pad's echo repeats, in ms (default 350; 1 .. ~2700 at
// 48 kHz). Works whether the echo is on or not; a running echo glides there.
// For a game: drumpad_set_echo_ms(d, pad, echo_ms_for_distance(meters)).
void drumpad_set_echo_ms(AT_Drumpad* d, int pad, int ms);

// Allocates a pad's echo now (control thread), so switching it on later
// allocates nothing: needed when the switching happens on the audio thread.
bool drumpad_reserve_echo(AT_Drumpad* d, int pad);

// ─── drumpad_pattern.c ───────────────────────────────────────────────────────
// For code (a game) as well as for the "*" editor. Control thread.

// Sets the pattern: rows (pads 1..rows, at most AT_DRUMPAD_PADS), steps (at
// most AT_PATTERN_MAX_STEPS), one bit mask per row (bit s = step s), the step
// time in ms. A playing pattern restarts with it. false = sizes out of range.
//     uint32_t beat[2] = { 0x5, 0xA };          // pad 1 on steps 0 and 2, pad 2 on 1 and 3
//     drumpad_pattern_set(d, 2, 4, beat, 500.0f);
//     drumpad_pattern_play(d);
bool drumpad_pattern_set (AT_Drumpad* d, int rows, int steps, const uint32_t* bits, float step_ms);
void drumpad_pattern_play(AT_Drumpad* d);   // from step 0, shortly after now
void drumpad_pattern_stop(AT_Drumpad* d);   // at once: hits sent ahead are dropped
int  drumpad_pattern_step(const AT_Drumpad* d);     // the step sounding now, -1 = not playing
void drumpad_pattern_tick(AT_Drumpad* d);   // sends the hits due within the lookahead (the instrument's tick)

// ─── drumpad_pattern_edit.c ──────────────────────────────────────────────────

void drumpad_pattern_edit_open(AT_Drumpad* d);
bool drumpad_pattern_edit_key (AT_Drumpad* d, const AT_KeyEvent* ev);  // while d->pat_stage >= 0
void drumpad_pattern_edit_status(const AT_Drumpad* d, char* out, size_t cap);

// true while one of the drumpad's editors ("/" or "*") takes every key
static inline bool drumpad_editing(const AT_Drumpad* d) { return d->edit_pad >= 0 || d->pat_stage >= 0; }

// ─── drumpad_events.c ────────────────────────────────────────────────────────

// Every later change is reported to fn(user, event). NULL stops it.
void   drumpad_set_observer(AT_Drumpad* d, AT_PadObserver fn, void* user);

// Reports the current settings of every loaded pad as events (pitch, tempo,
// volume, pan, loop, sync, cut, effects, then "loop start" for running loops):
// what a recording writes at its start.
void   drumpad_snapshot    (AT_Drumpad* d, AT_PadObserver fn, void* user);

// Does what the event says (through the drumpad_* functions).
void   drumpad_event_apply (AT_Drumpad* d, const AT_PadEvent* ev);

// Text form, one line without the time: "pad 2 S pitch +3", "pad 1 A loop start".
// The key label is for reading only; parsing goes by the pad number.
void   drumpad_event_format(const AT_PadEvent* ev, char* out, size_t cap);
bool   drumpad_event_parse (const char* text, AT_PadEvent* ev);     // false = not a pad event
// AT_ProcessFn — the pad's layer insert 1, user = AT_DrumPad*.
void drumpad_fx_process(AT_Buffer* buf, void* user);
extern const char* const AT_PAD_FX_NAMES[AT_PAD_FX_COUNT];

// ─── drumpad_clock.c ─────────────────────────────────────────────────────────

// The engine source of a drumpad (self = AT_Drumpad*): its layer stack, plus
// keeping the clock and each pad's own start time up to date.
extern const AT_SourceType AT_DRUMPAD_SOURCE;

// Audio thread: a pad's loop starts now. Returns how many frames ago it
// should have started: 0 (from its top), or for a synced pad joining a running
// clock, the clock's age, which keeps it in phase. Starts the clock if no
// synced loop runs.
double   drumpad_clock_loop_start(AT_Drumpad* d, int pad);

// Audio thread: the frame_clock at which the pad's time base is 0 (the shared
// clock's origin for a synced pad, its own last start for a free one).
uint64_t drumpad_clock_origin(const AT_DrumPad* p);

// ─── used between the files of this folder ───────────────────────────────────

void drumpad_emit(AT_Drumpad* d, AT_PadEventType type, int pad, int fx, float a, float b);    // drumpad_events.c
bool drumpad_key        (AT_Drumpad* d, const AT_KeyEvent* ev);         // drumpad_keys.c
bool drumpad_cut_edit_start(AT_Drumpad* d);                             // drumpad_cut_edit.c: false = no pad held
bool drumpad_cut_edit_key  (AT_Drumpad* d, int pad_of_key, const AT_KeyEvent* ev);   // while d->edit_pad >= 0
void drumpad_release_all(AT_Drumpad* d);                                // drumpad_keys.c
uint16_t drumpad_pad_key(int pad);                                      // drumpad_keys.c: scan code of a pad
void drumpad_build_help (AT_Drumpad* d);                                // drumpad_keys.c
