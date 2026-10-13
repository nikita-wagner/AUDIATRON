// ═══════════════════════════════════════════════════════════════════════════
//  app/app.h — the program: glues the reusable modules into audiatron.exe
//
//  This folder is the ONLY place that knows which concrete instruments and
//  effects exist. Everything outside app/ is a reusable module that knows
//  nothing about this program.
//
//  Files in this folder:
//    app_args.c          command line → AT_AppOptions
//    app_devices.c       --list, --info, choosing the backend, opening the device
//    app_setup.c         engine + master sound + tracks + effects
//    app_instruments.c   ◄ REGISTER NEW INSTRUMENTS HERE (one track each)
//    app_effects.c       ◄ REGISTER NEW EFFECTS HERE
//    app_keys.c          what a key press does (control thread callback)
//    app_record.c        F5 / F6 / F7: input and WAV recording
//    app_params.c        the names of instrument parameters in a recording
//    app_replay.c        plays a recording on copies of the tracks (the replay track)
//    app_status.c        status line + help text (control thread callback)
//    app_live.c          live mode: device → audio thread → control loop
//    app_offline.c       --offline: render a demo into a WAV file, no device
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "backend/select/backend_select.h"
#include "engine/engine.h"
#include "input/key_event.h"
#include "instruments/drumpad/drumpad.h"
#include "instruments/fusion/fusion.h"
#include "instruments/replay/replay.h"
#include "recording/event_log/event_log.h"
#include "recording/wav_recorder/wav_recorder.h"
#include "threads/audio/audio_thread.h"
#include "tracks/track_list.h"

#define AT_APP_MAX_EFFECTS      16
#define AT_APP_MAX_FX_FLAGS     8
#define AT_APP_MAX_SOUNDS       4           // --play / --sample files
#define AT_APP_MAX_PADS         16          // --pad files (one drumpad track)

typedef enum {
    AT_APP_MODE_LIVE,
    AT_APP_MODE_LIST,
    AT_APP_MODE_INFO,
    AT_APP_MODE_OFFLINE
} AT_AppMode;

// Part of a sound file, written after its name: "kick.mp3(0.5)" = the first
// half, "loop.wav(0.2,0.9)" = 20 % .. 90 %, "x.wav(0.25s,1.5s)" = in seconds.
// On a --pad two more values may follow: wait and play for (in ms or s;
// plain numbers are ms), "kick.wav(250ms,1000ms,2000ms,1000ms)".
typedef struct {
    bool  set;
    float from, to;                             // fractions of the length, or seconds
    bool  from_seconds, to_seconds;
    bool  timed;                                // wait / play for were given
    float wait_s, play_s;                       // seconds
} AT_AppCut;

typedef struct {
    AT_AppMode   mode;
    AT_BackendChoice backend;                   // auto = WASAPI where it exists, else WinMM
    bool         exclusive;                     // WASAPI exclusive mode
    int          device_index;                  // -1 = system default
    uint32_t     sample_rate;                   // 0 = device rate (offline: 48000)
    uint32_t     period_us;                     // 0 = device default
    uint32_t     queue_ms;                      // 0 = backend default (WinMM latency)
    const char*  offline_path;
    double       offline_seconds;
    int          track;                         // 1-based track the keyboard / the offline demo plays; 0 = automatic
    const char*  fx[AT_APP_MAX_FX_FLAGS];       // effects to switch on at startup (--fx name)
    uint32_t     fx_count;

    // sound files (--play FILE: a player track, --sample FILE: a keyboard-sampler track), in command-line order
    const char*  sound_path[AT_APP_MAX_SOUNDS];
    bool         sound_is_sampler[AT_APP_MAX_SOUNDS];
    AT_AppCut    sound_cut[AT_APP_MAX_SOUNDS];
    uint32_t     sound_count;
    float        pitch;                         // --pitch SEMITONES, start value for the players
    float        speed;                         // --speed FACTOR, start value for the players (0 = 1.0)
    bool         keep_tempo;                    // --keep-tempo: players start in keep-tempo mode

    // drumpad sounds (--pad FILE), pad 1, 2, ... in command-line order; then
    // the files of --pads FOLDER. With no sound options at all, the drumpad
    // loads the "sample" folder by itself (app_instruments.c).
    const char*  pad_path[AT_APP_MAX_PADS];
    AT_AppCut    pad_cut[AT_APP_MAX_PADS];
    uint32_t     pad_count;
    const char*  pad_folder;

    const char*  replay_path;                   // --replay FILE: a recording for the replay track
    const char*  record_dir;                    // --record-dir DIR, where F5 / F6 / F7 save (default "recordings")
} AT_AppOptions;

// One kind of effect the user can switch on. Every engine channel (each track,
// and each copy of a track the replay plays on) gets its OWN instance, made
// the first time the effect is switched on there, so an effect never acts on
// more than one track. It sits on the channel hook.
typedef struct {
    const char*  name;                          // status line label and --fx name
    uint16_t     hotkey;                        // AT_SC_* that toggles it on the selected track
    uint32_t     slot;                          // AT_HOOK_CHANNEL slot: the order effects run in
    AT_ProcessFn process;
    size_t       size;                          // bytes of one instance
    void       (*setup)(void* state, float sample_rate);    // gives a fresh instance the app's sound
    void*        state  [AT_ENGINE_CHANNELS];   // per channel, NULL = never switched on there
    bool         enabled[AT_ENGINE_CHANNELS];
} AT_AppEffect;

// What plays on a track, for recording and replaying it.
typedef enum {
    AT_APP_KIND_PIANO,
    AT_APP_KIND_LAYERS,
    AT_APP_KIND_PLAYER,
    AT_APP_KIND_SAMPLER,
    AT_APP_KIND_DRUMPAD,
    AT_APP_KIND_REPLAY,
    AT_APP_KIND_FUSION
} AT_AppKind;

typedef struct {
    AT_AppKind    kind;
    char          file[260];                    // player / sampler: the file, full path
    bool          has_copy;                     // a copy for the replay sits on channel + AT_TRACK_MAX
} AT_AppTrackInfo;

// Something on a track whose parameters a recording follows, e.g. the
// player's sampler: "track 5 sampler rate 1.12". The replay's copy of the
// track has the same thing at `copy`.
typedef struct {
    int          track;                         // 0-based
    const char*  name;                          // "synth", "layers", "sampler", "shifter"
    AT_ParamFn   fn;
    void*        live;
    void*        copy;
} AT_AppTarget;

#define AT_APP_MAX_TARGETS  32
#define AT_APP_ECHO_MS      350             // echo time a track starts with
#define AT_APP_ECHO_STEP_MS 25              // E + '+' / '-'
#define AT_APP_REPLAY_DRUMS (-2)

// What is being recorded right now (app_record.c).
typedef struct {
    bool           input_on;
    AT_EventLog    log;                     // the input recording: keys + pad events, as text
    char           input_path[260];

    bool           wav_on;
    AT_WavRecorder wav;                     // hooked on the engine output while recording
    char           wav_path[260];
    bool           wav_start_logged;        // the input log has the WAV's exact start

    int            take;                    // number in the file names (rec_001.txt / rec_001.wav)
} AT_AppRecord;

typedef struct {
    AT_AppOptions  options;

    AT_Engine      engine;
    AT_Backend     backend;
    AT_AudioThread audio_thread;

    AT_TrackList   tracks;                                  // one instrument per track; one is selected

    AT_AppEffect   effects[AT_APP_MAX_EFFECTS];
    uint32_t       effect_count;
    int            echo_ms[AT_ENGINE_CHANNELS];             // echo time per channel (its echo uses it when made)
    bool           e_held;                                  // the E key is down: '+' / '-' change the echo time

    float          sample_rate;                             // set by app_setup; new effect instances need it
    int            first_sound_track;                       // track of the first --play / --sample / --pad file, -1 = none

    float          master_volume;
    bool           master_eq_bypass;

    AT_Drumpad*    drumpad;                                 // the live drumpad, NULL = none
    AT_Replay*     replay;                                  // the replay track's instrument, NULL = none
    int            replay_track;                            // its track, -1 = none
    AT_Fusion*     fusion;                                  // the fusion track: keyboard + numpad drums, NULL = none
    int            fusion_track;                            // its track, -1 = none
    int            fusion_keyboard_track;                   // the tracks it plays
    int            fusion_drum_track;
    AT_AppRecord   record;

    AT_AppTrackInfo track_info[AT_TRACK_MAX];               // what each track is (app_instruments.c)
    AT_AppTarget   targets[AT_APP_MAX_TARGETS];             // parameters a recording follows
    uint32_t       target_count;

    // the replay's view of the recording's tracks (app_replay.c)
    int            replay_map[AT_TRACK_MAX];                // the recording's track t plays on the copy of this
                                                            // program's track replay_map[t]; -1 = nowhere,
                                                            // AT_APP_REPLAY_DRUMS = the replay's own drumpad
    bool           replay_mute[AT_TRACK_MAX];
    bool           replay_solo[AT_TRACK_MAX];
} AT_App;

// ─── app_args.c ──────────────────────────────────────────────────────────────
bool app_parse_args(AT_AppOptions* o, int argc, char** argv);
// A cut in fractions of a clip `seconds` long. false = it is empty or backwards.
bool app_cut_resolve(const AT_AppCut* cut, double seconds, float* from, float* to);

// ─── app_devices.c ───────────────────────────────────────────────────────────
int  app_run_list(const AT_App* app);
int  app_run_info(AT_App* app);
bool app_open_device(AT_App* app);                          // opens app->backend from the options

// ─── app_setup.c ─────────────────────────────────────────────────────────────
bool app_setup   (AT_App* app, float sample_rate);          // false = bad --fx name or --track number
void app_teardown(AT_App* app);

// ─── app_instruments.c ───────────────────────────────────────────────────────
bool app_instruments_setup(AT_App* app, float sample_rate);  // fills app->tracks; false = a sound file would not load
void app_instruments_teardown(AT_App* app);                  // frees the loaded sound files

// ─── app_effects.c ───────────────────────────────────────────────────────────
void app_effects_setup   (AT_App* app);
void app_effects_teardown(AT_App* app);                                 // the audio thread must be stopped
int  app_effect_find     (const AT_App* app, const char* name);         // -1 = unknown
// Switches effect `index` on one track only. false = no memory for it (stays off).
bool app_effect_set      (AT_App* app, uint32_t index, uint32_t track, bool enabled);
// The same on any engine channel (the replay's copies of tracks).
bool app_effect_set_channel(AT_App* app, uint32_t index, uint32_t channel, bool enabled);
bool app_effect_enabled  (const AT_App* app, uint32_t index, uint32_t track);

// The echo time of one track, in ms (1 .. ~2700 at 48 kHz). On the drumpad
// track it also sets every pad's echo. Works whether the echo is on or not; a
// running echo glides to it. For a game engine:
//     app_set_echo_ms(app, track, echo_ms_for_distance(meters_to_wall));
// Returns the time actually set (clamped).
int  app_set_echo_ms     (AT_App* app, uint32_t track, int ms);
int  app_echo_ms         (const AT_App* app, uint32_t track);
int  app_set_echo_ms_channel(AT_App* app, uint32_t channel, int ms);  // any channel (the replay's copies)

// ─── app_record.c ────────────────────────────────────────────────────────────
uint64_t app_now(const AT_App* app);                        // engine frames rendered so far (any thread)
bool app_engine_fence(AT_App* app);                         // waits until the audio thread applied every command sent so far
bool app_record_hotkey (AT_App* app, uint16_t scan);        // F5 / F6 / F7; true = handled
void app_record_key    (AT_App* app, const AT_KeyEvent* ev);   // logs a key while recording input
void app_record_note   (AT_App* app, const char* fmt, ...);    // logs an app event ("track 2 select layers")
void app_record_tick   (AT_App* app);                       // moves recorded audio to the WAV file
void app_record_status (const AT_App* app, char* out, size_t cap);
void app_record_shutdown(AT_App* app);                      // closes what is open (audio thread stopped)

// ─── app_params.c ────────────────────────────────────────────────────────────
// "sampler rate 1.12" ⇄ (sampler_set_param, AT_SAMPLER_PARAM_RATE, 1.12)
void app_param_format(const AT_AppTarget* t, int param, float value, char* out, size_t cap);
bool app_param_parse (const AT_AppTarget* t, const char* text, int* param, float* value);   // text after the target name
void app_target_snapshot(const AT_AppTarget* t, void (*emit)(void* user, const char* line), void* user);   // its state as lines
const AT_AppTarget* app_target_find_live(const AT_App* app, AT_ParamFn fn, const void* self);
const AT_AppTarget* app_target_find_name(const AT_App* app, int track, const char* name);

// ─── app_replay.c ────────────────────────────────────────────────────────────
void app_replay_setup   (AT_App* app);                      // after the tracks exist
void app_replay_teardown(AT_App* app);
bool app_replay_load    (AT_App* app, const char* path);    // a recording into the replay track (also while running)
void app_background_tick(AT_App* app);                      // every tick / block: WAV recording, replay lookahead
const char* app_kind_name(AT_AppKind kind);                 // "piano", "layers", "player", ...

// ─── app_keys.c / app_status.c — control thread callbacks (user = AT_App*) ───
bool app_on_key (void* user, const AT_KeyEvent* ev);
bool app_on_tick(void* user);
void app_print_help(const AT_App* app);

// ─── modes ───────────────────────────────────────────────────────────────────
int app_run_live   (AT_App* app);                           // app_live.c
int app_run_offline(AT_App* app);                           // app_offline.c
