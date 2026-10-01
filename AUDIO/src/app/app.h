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
//    app_status.c        status line + help text (control thread callback)
//    app_live.c          live mode: device → audio thread → control loop
//    app_offline.c       --offline: render a demo into a WAV file, no device
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "backend/select/backend_select.h"
#include "engine/engine.h"
#include "input/key_event.h"
#include "threads/audio/audio_thread.h"
#include "tracks/track_list.h"

#define AT_APP_MAX_EFFECTS      16
#define AT_APP_MAX_FX_FLAGS     8

typedef enum {
    AT_APP_MODE_LIVE,
    AT_APP_MODE_LIST,
    AT_APP_MODE_INFO,
    AT_APP_MODE_OFFLINE
} AT_AppMode;

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
    int          track;                         // 1-based track the keyboard / the offline demo plays
    const char*  fx[AT_APP_MAX_FX_FLAGS];       // effects to switch on at startup (--fx name)
    uint32_t     fx_count;
} AT_AppOptions;

// One effect the user can toggle: where it sits in the signal graph + its hotkey.
typedef struct {
    const char*  name;                          // status line label and --fx name
    uint16_t     hotkey;                        // AT_SC_* that toggles it
    AT_HookPoint point;
    uint32_t     channel;                       // ignored for master hook points
    uint32_t     slot;
    AT_ProcessFn process;
    void*        state;
    bool         enabled;
} AT_AppEffect;

typedef struct {
    AT_AppOptions  options;

    AT_Engine      engine;
    AT_Backend     backend;
    AT_AudioThread audio_thread;

    AT_TrackList   tracks;                                  // one instrument per track; one is selected

    AT_AppEffect   effects[AT_APP_MAX_EFFECTS];
    uint32_t       effect_count;

    float          master_volume;
    bool           master_eq_bypass;
} AT_App;

// ─── app_args.c ──────────────────────────────────────────────────────────────
bool app_parse_args(AT_AppOptions* o, int argc, char** argv);

// ─── app_devices.c ───────────────────────────────────────────────────────────
int  app_run_list(const AT_App* app);
int  app_run_info(AT_App* app);
bool app_open_device(AT_App* app);                          // opens app->backend from the options

// ─── app_setup.c ─────────────────────────────────────────────────────────────
bool app_setup   (AT_App* app, float sample_rate);          // false = bad --fx name or --track number
void app_teardown(AT_App* app);

// ─── app_instruments.c ───────────────────────────────────────────────────────
void app_instruments_setup(AT_App* app, float sample_rate);  // fills app->tracks

// ─── app_effects.c ───────────────────────────────────────────────────────────
void app_effects_setup(AT_App* app, float sample_rate);
int  app_effect_find  (const AT_App* app, const char* name);            // -1 = unknown
void app_effect_set   (AT_App* app, uint32_t index, bool enabled);

// ─── app_keys.c / app_status.c — control thread callbacks (user = AT_App*) ───
bool app_on_key (void* user, const AT_KeyEvent* ev);
bool app_on_tick(void* user);
void app_print_help(const AT_App* app);

// ─── modes ───────────────────────────────────────────────────────────────────
int app_run_live   (AT_App* app);                           // app_live.c
int app_run_offline(AT_App* app);                           // app_offline.c
