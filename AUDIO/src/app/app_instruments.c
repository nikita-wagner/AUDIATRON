// ═══════════════════════════════════════════════════════════════════════════
//  app/app_instruments.c — ◄ REGISTER NEW INSTRUMENTS HERE
//
//  Every instrument gets its own TRACK: its own mixer channel with EQ, effects
//  and fader (tracks/track_list.h). Tab selects the track the keyboard plays.
//
//  To add an instrument (after creating its folder under instruments/):
//    1. #include its header
//    2. add a static instance
//    3. init it and track_list_add() it in app_instruments_setup
//
//  To layer several sounds into ONE instrument instead, see
//  instruments/layered_keyboard/ (layers/layer_stack.h).
//
//  Sound files (--play / --sample on the command line) become extra tracks
//  after the built-in ones, and all --pad / --pads files share one drumpad
//  track after those. Without any of these options the drumpad loads the
//  "sample" folder by itself. They own memory (the decoded files), so they are
//  loaded here and freed in app_instruments_teardown.
//
//  The replay plays recordings on COPIES of the tracks, so the tracks stay
//  free to play live. Every melodic track gets its copy here, on the engine
//  channel AT_TRACK_MAX above its own (players and sampler keyboards share the
//  decoded file with their copy). The drums of a recording play on the replay
//  track's own drumpad (app_replay.c). A new instrument that should be
//  recorded and replayed: make its copy in register_track and list the
//  modules it changes with engine_set_param as targets.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "instruments/drumpad/drumpad.h"
#include "instruments/fusion/fusion.h"
#include "instruments/keyboard_piano/keyboard_piano.h"
#include "instruments/layered_keyboard/layered_keyboard.h"
#include "instruments/replay/replay.h"
#include "instruments/sample_keyboard/sample_keyboard.h"
#include "instruments/sample_player/sample_player.h"
#include "io/sound_folder/sound_folder.h"

// The app is the composition root, so it owns the instances.
static AT_KeyboardPiano   s_piano;
static AT_LayeredKeyboard s_layers;
static AT_SamplePlayer    s_players [AT_APP_MAX_SOUNDS];
static AT_SampleKeyboard  s_samplers[AT_APP_MAX_SOUNDS];
static bool               s_loaded_player [AT_APP_MAX_SOUNDS];
static bool               s_loaded_sampler[AT_APP_MAX_SOUNDS];
static AT_Drumpad         s_drumpad;                // ~1 MB: a pitch shifter per pad
static bool               s_drumpad_loaded;
static AT_Replay          s_replay;
static bool               s_replay_ready;
static AT_Fusion          s_fusion;                 // plays the layers track and the drumpad together
static int                s_layers_track = -1;
static int                s_drum_track   = -1;

// the replay's copies of the melodic tracks
static AT_KeyboardPiano   s_piano_copy;
static AT_LayeredKeyboard s_layers_copy;
static AT_SamplePlayer    s_players_copy [AT_APP_MAX_SOUNDS];
static AT_SampleKeyboard  s_samplers_copy[AT_APP_MAX_SOUNDS];

_Static_assert(AT_APP_MAX_PADS <= AT_DRUMPAD_PADS, "more --pad files than pads");

// ─── What each track is, its copy, the parameters a recording follows ───────

static void add_target(AT_App* app, int track, const char* name, AT_ParamFn fn, void* live, void* copy) {
    if (app->target_count == AT_APP_MAX_TARGETS) return;
    app->targets[app->target_count++] = (AT_AppTarget){ track, name, fn, live, copy };
}

// Notes `kind` (and `file`) for the track and plugs `copy` (if any) into the
// channel AT_TRACK_MAX above the track's own, set like a new track.
static void register_track(AT_App* app, int track, AT_AppKind kind, const char* file, const AT_Instrument* copy) {
    AT_AppTrackInfo* info = &app->track_info[track];
    info->kind     = kind;
    info->has_copy = copy != NULL;
    info->file[0]  = '\0';
    if (file && !GetFullPathNameA(file, sizeof info->file, info->file, NULL))
        snprintf(info->file, sizeof info->file, "%s", file);

    if (!copy) return;
    uint32_t channel = app->tracks.tracks[track].channel + AT_TRACK_MAX;
    copy->attach(copy->self, &app->engine, channel);
    engine_set_channel_volume(&app->engine, channel, app->tracks.tracks[track].volume);
    engine_set_channel_pan(&app->engine, channel, app->tracks.tracks[track].pan);
}

// One track per sound file given on the command line.
static bool add_sound_tracks(AT_App* app, float sample_rate) {
    const AT_AppOptions* o = &app->options;

    app->first_sound_track = -1;

    for (uint32_t i = 0; i < o->sound_count; i++) {
        char error[160] = "";
        AT_Instrument instrument;
        float from, to;

        if (o->sound_is_sampler[i]) {
            if (!sample_keyboard_init(&s_samplers[i], o->sound_path[i], sample_rate, error, sizeof error)) goto failed;
            s_loaded_sampler[i] = true;
            if (!app_cut_resolve(&o->sound_cut[i], audio_clip_seconds(&s_samplers[i].clip), &from, &to)) goto bad_cut;
            sample_keyboard_set_cut(&s_samplers[i], from, to);
            sample_keyboard_init_copy(&s_samplers_copy[i], &s_samplers[i], sample_rate);
            instrument = sample_keyboard_instrument(&s_samplers[i]);
        } else {
            if (!sample_player_init(&s_players[i], o->sound_path[i], sample_rate, error, sizeof error)) goto failed;
            s_loaded_player[i] = true;
            if (!app_cut_resolve(&o->sound_cut[i], audio_clip_seconds(&s_players[i].clip), &from, &to)) goto bad_cut;

            // start values, written directly: the player is not plugged in yet
            sample_player_set_speed(&s_players[i], o->speed > 0.0f ? o->speed : 1.0f);
            sample_player_set_keep_tempo(&s_players[i], o->keep_tempo);
            sample_player_set_pitch(&s_players[i], o->pitch);
            sample_player_set_cut(&s_players[i], from, to);
            sample_player_init_copy(&s_players_copy[i], &s_players[i], sample_rate);
            instrument = sample_player_instrument(&s_players[i]);
        }

        int track = track_list_add(&app->tracks, instrument);
        if (track < 0) { fprintf(stderr, "no free track for %s (8 tracks at most)\n", o->sound_path[i]); return false; }

        if (o->sound_is_sampler[i]) {
            AT_Instrument copy = sample_keyboard_instrument(&s_samplers_copy[i]);
            register_track(app, track, AT_APP_KIND_SAMPLER, o->sound_path[i], &copy);
            add_target(app, track, "sampler", sampler_set_param, &s_samplers[i].sampler, &s_samplers_copy[i].sampler);
        } else {
            AT_Instrument copy = sample_player_instrument(&s_players_copy[i]);
            register_track(app, track, AT_APP_KIND_PLAYER, o->sound_path[i], &copy);
            add_target(app, track, "sampler", sampler_set_param,     &s_players[i].sampler, &s_players_copy[i].sampler);
            add_target(app, track, "shifter", pitch_shift_set_param, &s_players[i].shifter, &s_players_copy[i].shifter);
        }

        if (app->first_sound_track < 0) app->first_sound_track = track;
        continue;

    failed:
        fprintf(stderr, "cannot load \"%s\": %s\n", o->sound_path[i], error);
        return false;
    bad_cut:
        fprintf(stderr, "\"%s\": the part to play is empty (from must come before to, inside the file)\n", o->sound_path[i]);
        return false;
    }
    return true;
}

// Where the default kit is looked for: next to where the program is started,
// then next to the executable (build\audiatron.exe → ..\..\sample).
static const char* const k_kit_places[] = { "sample", "..\\sample", "..\\..\\sample" };

static bool find_default_kit(char* out, size_t cap) {
    char exe_dir[AT_SOUND_FOLDER_PATH] = "";
    DWORD n = GetModuleFileNameA(NULL, exe_dir, sizeof exe_dir);
    if (n > 0 && n < sizeof exe_dir) {
        char* slash = strrchr(exe_dir, '\\');
        if (slash) *slash = '\0';
    } else {
        exe_dir[0] = '\0';
    }

    for (int base = 0; base < 2; base++) {
        for (size_t i = 0; i < sizeof k_kit_places / sizeof *k_kit_places; i++) {
            if (base == 0) snprintf(out, cap, "%s", k_kit_places[i]);
            else if (exe_dir[0]) snprintf(out, cap, "%s\\%s", exe_dir, k_kit_places[i]);
            else continue;
            if (sound_folder_exists(out)) return true;
        }
    }
    return false;
}

// One drumpad track: the --pad files first (pad 1, 2, ... in command-line
// order), then the files of --pads FOLDER. Started without any sound option,
// the drumpad loads the "sample" folder, so it is there without typing anything.
static bool add_drumpad_track(AT_App* app, float sample_rate) {
    const AT_AppOptions* o = &app->options;

    static char folder_paths[AT_DRUMPAD_PADS][AT_SOUND_FOLDER_PATH];
    char        default_kit[AT_SOUND_FOLDER_PATH];
    const char* folder = o->pad_folder;
    if (!folder && o->pad_count == 0 && o->sound_count == 0 && find_default_kit(default_kit, sizeof default_kit))
        folder = default_kit;

    uint32_t folder_count = 0;
    if (folder) {
        if (!sound_folder_exists(folder)) { fprintf(stderr, "no folder \"%s\"\n", folder); return false; }
        folder_count = sound_folder_list(folder, folder_paths, AT_DRUMPAD_PADS - o->pad_count);
        if (folder_count == 0) printf("no WAV / MP3 files in \"%s\"\n", folder);
    }
    if (o->pad_count + folder_count == 0) return true;

    drumpad_init(&s_drumpad, sample_rate);
    s_drumpad_loaded = true;

    int pad = 0;
    for (uint32_t i = 0; i < o->pad_count; i++) {
        char error[160] = "";
        if (!drumpad_load(&s_drumpad, pad, o->pad_path[i], error, sizeof error)) {
            fprintf(stderr, "cannot load \"%s\": %s\n", o->pad_path[i], error);
            return false;
        }
        float from, to;
        if (!app_cut_resolve(&o->pad_cut[i], audio_clip_seconds(&s_drumpad.pads[pad].clip), &from, &to)) {
            fprintf(stderr, "\"%s\": the part to play is empty (from must come before to, inside the file)\n", o->pad_path[i]);
            return false;
        }
        // In time units ("250ms", "1.5s") the cut is set in milliseconds: frame-exact,
        // so pads cut to the same length loop in step.
        const AT_AppCut* cut = &o->pad_cut[i];
        if (cut->set && cut->to_seconds && (cut->from_seconds || cut->from == 0.0f))
            drumpad_set_cut_ms(&s_drumpad, pad, cut->from * 1000.0f, cut->to * 1000.0f);
        else
            drumpad_set_cut(&s_drumpad, pad, from, to);
        if (cut->timed) drumpad_set_timing(&s_drumpad, pad, cut->wait_s * 1000.0f, cut->play_s * 1000.0f);
        pad++;
    }
    // A folder may hold a file that does not decode: skip it, keep the rest.
    for (uint32_t i = 0; i < folder_count; i++) {
        char error[160] = "";
        if (drumpad_load(&s_drumpad, pad, folder_paths[i], error, sizeof error)) pad++;
        else fprintf(stderr, "skipped \"%s\": %s\n", folder_paths[i], error);
    }
    if (folder) printf("drumpad: %d sound(s) from \"%s\"\n", pad - (int)o->pad_count, folder);
    if (drumpad_loaded_count(&s_drumpad) == 0) return true;

    int track = track_list_add(&app->tracks, drumpad_instrument(&s_drumpad));
    if (track < 0) { fprintf(stderr, "no free track for the drumpad (8 tracks at most)\n"); return false; }

    if (app->first_sound_track < 0) app->first_sound_track = track;
    app->drumpad = &s_drumpad;
    s_drum_track = track;
    register_track(app, track, AT_APP_KIND_DRUMPAD, NULL, NULL);    // its recording plays on the replay's drumpad
    return true;
}

// The replay track: plays recordings back (an input recording loads into it
// when it stops; --replay loads one at start, see app_replay.c).
static bool add_replay_track(AT_App* app, float sample_rate) {
    const AT_AppOptions* o = &app->options;

    replay_init(&s_replay, sample_rate, (AT_ReplayHost){ 0 });    // the host is set by app_replay_setup
    s_replay_ready = true;

    int track = track_list_add(&app->tracks, replay_instrument(&s_replay));
    if (track < 0) {
        fprintf(stderr, "no free track for the replay (8 tracks at most)\n");
        return o->replay_path == NULL;                      // only an error if it was asked for
    }
    app->replay       = &s_replay;
    app->replay_track = track;
    register_track(app, track, AT_APP_KIND_REPLAY, NULL, NULL);
    return true;
}

// The fusion track: the main keyboard plays the layers track, the numpad the
// drumpad, both at once. It plays those tracks; it has no sound of its own.
static void add_fusion_track(AT_App* app) {
    if (!app->drumpad || s_layers_track < 0) return;

    fusion_init(&s_fusion, app->tracks.tracks[s_layers_track].instrument, app->drumpad);
    int track = track_list_add(&app->tracks, fusion_instrument(&s_fusion));
    if (track < 0) { printf("no free track for the fusion track (8 tracks at most)\n"); return; }

    app->fusion                = &s_fusion;
    app->fusion_track          = track;
    app->fusion_keyboard_track = s_layers_track;
    app->fusion_drum_track     = s_drum_track;
    register_track(app, track, AT_APP_KIND_FUSION, NULL, NULL);
}

void app_instruments_teardown(AT_App* app) {
    for (int i = 0; i < AT_APP_MAX_SOUNDS; i++) {
        if (s_loaded_player[i])  { sample_player_free(&s_players[i]);    s_loaded_player[i]  = false; }
        if (s_loaded_sampler[i]) { sample_keyboard_free(&s_samplers[i]); s_loaded_sampler[i] = false; }
    }
    for (int i = 0; i < AT_APP_MAX_SOUNDS; i++) {             // the copies share the files: no file is freed twice
        sample_player_free(&s_players_copy[i]);
        sample_keyboard_free(&s_samplers_copy[i]);
    }
    if (s_drumpad_loaded) { drumpad_free(&s_drumpad); s_drumpad_loaded = false; }
    if (s_replay_ready)   { replay_unload(&s_replay); s_replay_ready   = false; }
    app->target_count = 0;
    app->drumpad      = NULL;
    app->replay       = NULL;
    app->replay_track = -1;
    app->fusion       = NULL;
    app->fusion_track = -1;
}

bool app_instruments_setup(AT_App* app, float sample_rate) {
    track_list_init(&app->tracks, &app->engine);
    app->drumpad      = NULL;
    app->replay       = NULL;
    app->replay_track = -1;
    app->fusion       = NULL;
    app->fusion_track = -1;
    s_layers_track    = -1;
    s_drum_track      = -1;

    app->target_count = 0;
    memset(app->track_info, 0, sizeof app->track_info);

    // track 1: the plain piano
    keyboard_piano_init(&s_piano, sample_rate);
    keyboard_piano_init(&s_piano_copy, sample_rate);
    int piano = track_list_add(&app->tracks, keyboard_piano_instrument(&s_piano));
    AT_Instrument piano_copy = keyboard_piano_instrument(&s_piano_copy);
    register_track(app, piano, AT_APP_KIND_PIANO, NULL, &piano_copy);
    add_target(app, piano, "synth", synth_set_param, &s_piano.synth, &s_piano_copy.synth);

    // track 2: piano + strings + synth layered into one sound
    layered_keyboard_init(&s_layers, sample_rate);
    layered_keyboard_init(&s_layers_copy, sample_rate);
    int layers = track_list_add(&app->tracks, layered_keyboard_instrument(&s_layers));
    s_layers_track = layers;
    AT_Instrument layers_copy = layered_keyboard_instrument(&s_layers_copy);
    register_track(app, layers, AT_APP_KIND_LAYERS, NULL, &layers_copy);
    add_target(app, layers, "layers", layer_stack_set_param, &s_layers.stack, &s_layers_copy.stack);

    // tracks 3...: the sound files from the command line
    if (!add_sound_tracks(app, sample_rate)) { app_instruments_teardown(app); return false; }

    // then: the drumpad, if there are --pad files (or a kit folder)
    if (!add_drumpad_track(app, sample_rate)) { app_instruments_teardown(app); return false; }

    // then: the replay track, which plays input recordings back
    if (!add_replay_track(app, sample_rate)) { app_instruments_teardown(app); return false; }

    // last: the fusion track (keyboard = layers, numpad = drumpad)
    add_fusion_track(app);
    return true;
}
