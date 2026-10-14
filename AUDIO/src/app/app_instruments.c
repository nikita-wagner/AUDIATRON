// ═══════════════════════════════════════════════════════════════════════════
//  app/app_instruments.c — ◄ REGISTER NEW INSTRUMENTS HERE
//
//  Every instrument gets its own TRACK: its own mixer channel with EQ, effects
//  and fader (tracks/track_list.h). Tab selects the track the keyboard plays.
//
//  The tracks come from a TRACK LIST (AT_AppTrackSpec): what instrument each
//  track is and its files. Any instrument any number of times: three pianos
//  with their own sounds, two drumpads with different kits, an engine per car.
//  AudioStudio passes its list (options.track_specs); the console builds its
//  usual one from the command line (console_tracks below): piano, layered
//  keys, a track per --play / --sample file, the drumpad (--pad / --pads, or
//  the "sample" folder), the machine (--machine).
//
//  To add an instrument (after creating its folder under instruments/):
//    1. a kind in AT_AppKind (app.h) and its name in app_kind_name (app_replay.c)
//    2. a case in add_track below: make it (and its copy), track_list_add it,
//       register_track it, list its targets
//    3. its free in free_slot
//
//  Recordings play on COPIES of the tracks (app_replay.c), so the tracks stay
//  free to play live: every track gets its copy here, on the engine channel
//  AT_TRACK_MAX above its own (copies share the decoded files). An instrument
//  that should be recorded and replayed lists the modules it changes with
//  engine_set_param as targets.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "instruments/drumpad/drumpad.h"
#include "instruments/keyboard_piano/keyboard_piano.h"
#include "instruments/layered_keyboard/layered_keyboard.h"
#include "instruments/machine/machine.h"
#include "instruments/sample_keyboard/sample_keyboard.h"
#include "instruments/sample_player/sample_player.h"
#include "io/sound_folder/sound_folder.h"

// The app is the composition root, so it owns the instances: one per track.
typedef struct {
    AT_AppKind kind;
    void*      live;                    // the instrument (heap)
    void*      copy;                    // its copy for recordings, NULL = none
    bool       made;                    // initialised: free_slot frees what it holds
} Slot;

static Slot s_slots[AT_TRACK_MAX];

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
    info->copy     = copy ? copy->self : NULL;
    info->file[0]  = '\0';
    if (file && !GetFullPathNameA(file, sizeof info->file, info->file, NULL))
        snprintf(info->file, sizeof info->file, "%s", file);

    if (!copy) return;
    uint32_t channel = app->tracks.tracks[track].channel + AT_TRACK_MAX;
    copy->attach(copy->self, &app->engine, channel);
    engine_set_channel_volume(&app->engine, channel, app->tracks.tracks[track].volume);
    engine_set_channel_pan(&app->engine, channel, app->tracks.tracks[track].pan);
}

static void* zalloc(size_t size) {
    void* p = calloc(1, size);
    if (!p) fprintf(stderr, "out of memory\n");
    return p;
}

// ─── The default kit ─────────────────────────────────────────────────────────

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

// ─── Making one track ────────────────────────────────────────────────────────

static bool is_engine_file(const char* path) {
    const char* dot = strrchr(path, '.');
    return dot && !_stricmp(dot, ".atengine");
}

// The pads of a drumpad: its files first (pad 1, 2, ...), then those of its
// folder. A folder file that does not decode is skipped; a named file is an error.
static bool load_pads(AT_Drumpad* d, const AT_AppTrackSpec* spec) {
    static char folder_paths[AT_DRUMPAD_PADS][AT_SOUND_FOLDER_PATH];
    char        default_kit[AT_SOUND_FOLDER_PATH];
    const char* folder = spec->pad_folder;
    if (!folder && spec->pad_count == 0 && spec->default_kit && find_default_kit(default_kit, sizeof default_kit))
        folder = default_kit;

    uint32_t folder_count = 0;
    if (folder) {
        if (!sound_folder_exists(folder)) { fprintf(stderr, "no folder \"%s\"\n", folder); return false; }
        folder_count = sound_folder_list(folder, folder_paths, AT_DRUMPAD_PADS - spec->pad_count);
        if (folder_count == 0) printf("no WAV / MP3 files in \"%s\"\n", folder);
    }

    int pad = 0;
    for (uint32_t i = 0; i < spec->pad_count; i++) {
        char error[160] = "";
        if (!drumpad_load(d, pad, spec->pads[i], error, sizeof error)) {
            fprintf(stderr, "cannot load \"%s\": %s\n", spec->pads[i], error);
            return false;
        }
        const AT_AppCut* cut = spec->pad_cuts[i];
        if (cut && cut->set) {
            float from, to;
            if (!app_cut_resolve(cut, audio_clip_seconds(&d->pads[pad].clip), &from, &to)) {
                fprintf(stderr, "\"%s\": the part to play is empty (from must come before to, inside the file)\n", spec->pads[i]);
                return false;
            }
            // In time units ("250ms", "1.5s") the cut is set in milliseconds: frame-exact,
            // so pads cut to the same length loop in step.
            if (cut->to_seconds && (cut->from_seconds || cut->from == 0.0f))
                drumpad_set_cut_ms(d, pad, cut->from * 1000.0f, cut->to * 1000.0f);
            else
                drumpad_set_cut(d, pad, from, to);
            if (cut->timed) drumpad_set_timing(d, pad, cut->wait_s * 1000.0f, cut->play_s * 1000.0f);
        }
        pad++;
    }
    for (uint32_t i = 0; i < folder_count; i++) {
        char error[160] = "";
        if (drumpad_load(d, pad, folder_paths[i], error, sizeof error)) pad++;
        else fprintf(stderr, "skipped \"%s\": %s\n", folder_paths[i], error);
    }
    if (folder) printf("drumpad: %d sound(s) from \"%s\"\n", pad - (int)spec->pad_count, folder);
    return true;
}

// The machine's sound: a built-in one (with its settings), a saved engine, a
// sound file, or (NULL) the built-in petrol engine.
static bool load_machine(AT_Machine* m, const char* file) {
    AT_MachineBuiltin builtin = AT_MACHINE_BUILTIN_PETROL;
    char error[300] = "";
    if (!file) return true;
    if (machine_find_builtin(file, &builtin)) {
        if (!machine_load_builtin(m, builtin)) { fprintf(stderr, "machine: out of memory\n"); return false; }
        machine_preset(m, builtin);
        return true;
    }
    const bool ok = is_engine_file(file) ? machine_load_preset(m, file, AT_MACHINE_SAVE_ALL, error, sizeof error)
                                         : machine_load(m, file, error, sizeof error);
    if (!ok) fprintf(stderr, "cannot load \"%s\": %s\n", file, error);
    return ok;
}

enum { ADDED, FAILED, LEFT_OUT };

// Makes the instrument of one track list entry and adds it as a track.
// FAILED = a file would not load or no track was free (reported).
static int add_track(AT_App* app, float sample_rate, const AT_AppTrackSpec* spec, int index) {
    const AT_AppOptions* o    = &app->options;
    Slot*                slot = &s_slots[index];
    char                 error[160] = "";
    AT_Instrument        live, copy;
    float                from = 0.0f, to = 1.0f;
    bool                 has_copy = true;
    slot->kind = spec->kind;

    switch (spec->kind) {
    case AT_APP_KIND_PIANO: {
        AT_KeyboardPiano *p = zalloc(sizeof *p), *c = zalloc(sizeof *c);
        slot->live = p; slot->copy = c;
        if (!p || !c) return FAILED;
        keyboard_piano_init(p, sample_rate);
        keyboard_piano_init(c, sample_rate);
        slot->made = true;
        live = keyboard_piano_instrument(p);
        copy = keyboard_piano_instrument(c);
        break;
    }
    case AT_APP_KIND_LAYERS: {
        AT_LayeredKeyboard *k = zalloc(sizeof *k), *c = zalloc(sizeof *c);
        slot->live = k; slot->copy = c;
        if (!k || !c) return FAILED;
        layered_keyboard_init(k, sample_rate);
        layered_keyboard_init(c, sample_rate);
        slot->made = true;
        live = layered_keyboard_instrument(k);
        copy = layered_keyboard_instrument(c);
        break;
    }
    case AT_APP_KIND_PLAYER: {
        AT_SamplePlayer *p = zalloc(sizeof *p), *c = zalloc(sizeof *c);
        slot->live = p; slot->copy = c;
        if (!p || !c || !spec->file) return FAILED;
        if (!sample_player_init(p, spec->file, sample_rate, error, sizeof error)) {
            fprintf(stderr, "cannot load \"%s\": %s\n", spec->file, error);
            return FAILED;
        }
        slot->made = true;
        if (spec->cut && !app_cut_resolve(spec->cut, audio_clip_seconds(&p->clip), &from, &to)) {
            fprintf(stderr, "\"%s\": the part to play is empty (from must come before to, inside the file)\n", spec->file);
            return FAILED;
        }
        // start values, written directly: the player is not plugged in yet
        sample_player_set_speed(p, o->speed > 0.0f ? o->speed : 1.0f);
        sample_player_set_keep_tempo(p, o->keep_tempo);
        sample_player_set_pitch(p, o->pitch);
        sample_player_set_cut(p, from, to);
        sample_player_init_copy(c, p, sample_rate);
        live = sample_player_instrument(p);
        copy = sample_player_instrument(c);
        break;
    }
    case AT_APP_KIND_SAMPLER: {
        AT_SampleKeyboard *k = zalloc(sizeof *k), *c = zalloc(sizeof *c);
        slot->live = k; slot->copy = c;
        if (!k || !c || !spec->file) return FAILED;
        if (!sample_keyboard_init(k, spec->file, sample_rate, error, sizeof error)) {
            fprintf(stderr, "cannot load \"%s\": %s\n", spec->file, error);
            return FAILED;
        }
        slot->made = true;
        if (spec->cut && !app_cut_resolve(spec->cut, audio_clip_seconds(&k->clip), &from, &to)) {
            fprintf(stderr, "\"%s\": the part to play is empty (from must come before to, inside the file)\n", spec->file);
            return FAILED;
        }
        sample_keyboard_set_cut(k, from, to);
        sample_keyboard_init_copy(c, k, sample_rate);
        live = sample_keyboard_instrument(k);
        copy = sample_keyboard_instrument(c);
        break;
    }
    case AT_APP_KIND_DRUMPAD: {
        AT_Drumpad *d = zalloc(sizeof *d), *c = zalloc(sizeof *c);
        slot->live = d; slot->copy = c;
        if (!d || !c) return FAILED;
        drumpad_init(d, sample_rate);
        drumpad_init(c, sample_rate);
        slot->made = true;
        if (!load_pads(d, spec)) return FAILED;
        if (spec->default_kit && drumpad_loaded_count(d) == 0) return LEFT_OUT;   // the console's kit folder is not there
        drumpad_init_copy(c, d, sample_rate);                   // the same files, its own settings
        live = drumpad_instrument(d);
        copy = drumpad_instrument(c);
        break;
    }
    case AT_APP_KIND_MACHINE: {
        AT_Machine *m = zalloc(sizeof *m), *c = zalloc(sizeof *c);
        slot->live = m; slot->copy = c;
        if (!m || !c) return FAILED;
        if (!machine_init(m, sample_rate)) { fprintf(stderr, "machine: out of memory\n"); return FAILED; }
        slot->made = true;
        if (!load_machine(m, spec->file)) return FAILED;
        machine_init_copy(c, m, sample_rate);
        live = machine_instrument(m);
        copy = machine_instrument(c);
        break;
    }
    case AT_APP_KIND_AUDIO: {
        AT_AudioTrack* t = zalloc(sizeof *t);
        slot->live = t;
        if (!t) return FAILED;
        audio_track_init(t, sample_rate);
        slot->made = true;
        live = audio_track_instrument(t);
        has_copy = false;                                       // its regions are not in recordings
        break;
    }
    default:
        return FAILED;
    }

    int track = track_list_add(&app->tracks, live);
    if (track < 0) { fprintf(stderr, "no free track for %s (%d tracks at most)\n", live.name, AT_TRACK_MAX); return FAILED; }
    register_track(app, track, spec->kind, spec->kind == AT_APP_KIND_PLAYER || spec->kind == AT_APP_KIND_SAMPLER ? spec->file : NULL,
                   has_copy ? &copy : NULL);
    app->track_info[track].spec = index;

    switch (spec->kind) {
    case AT_APP_KIND_PIANO:
        add_target(app, track, "synth", synth_set_param, &((AT_KeyboardPiano*)slot->live)->synth, &((AT_KeyboardPiano*)slot->copy)->synth);
        break;
    case AT_APP_KIND_LAYERS:
        add_target(app, track, "layers", layer_stack_set_param, &((AT_LayeredKeyboard*)slot->live)->stack, &((AT_LayeredKeyboard*)slot->copy)->stack);
        break;
    case AT_APP_KIND_PLAYER:
        add_target(app, track, "sampler", sampler_set_param, &((AT_SamplePlayer*)slot->live)->sampler, &((AT_SamplePlayer*)slot->copy)->sampler);
        add_target(app, track, "shifter", pitch_shift_set_param, &((AT_SamplePlayer*)slot->live)->shifter, &((AT_SamplePlayer*)slot->copy)->shifter);
        break;
    case AT_APP_KIND_SAMPLER:
        add_target(app, track, "sampler", sampler_set_param, &((AT_SampleKeyboard*)slot->live)->sampler, &((AT_SampleKeyboard*)slot->copy)->sampler);
        break;
    case AT_APP_KIND_MACHINE:
        add_target(app, track, "machine", machine_set_param, &((AT_Machine*)slot->live)->sound, &((AT_Machine*)slot->copy)->sound);
        break;
    default:
        break;
    }
    if (spec->kind == AT_APP_KIND_DRUMPAD && !app->drumpad) app->drumpad = (AT_Drumpad*)slot->live;
    if (spec->kind == AT_APP_KIND_AUDIO && !app->audio) {
        app->audio       = (AT_AudioTrack*)slot->live;
        app->audio_track = track;
    }
    if (app->first_sound_track < 0 && spec->from_command_line) app->first_sound_track = track;
    return ADDED;
}

static void free_slot(Slot* slot) {
    if (slot->made) {
        switch (slot->kind) {
        case AT_APP_KIND_PLAYER:                                // copies first: they share the files
            sample_player_free((AT_SamplePlayer*)slot->copy);
            sample_player_free((AT_SamplePlayer*)slot->live);
            break;
        case AT_APP_KIND_SAMPLER:
            sample_keyboard_free((AT_SampleKeyboard*)slot->copy);
            sample_keyboard_free((AT_SampleKeyboard*)slot->live);
            break;
        case AT_APP_KIND_DRUMPAD:
            drumpad_free((AT_Drumpad*)slot->copy);
            drumpad_free((AT_Drumpad*)slot->live);
            break;
        case AT_APP_KIND_MACHINE:
            if (slot->copy) machine_free((AT_Machine*)slot->copy);
            machine_free((AT_Machine*)slot->live);
            break;
        case AT_APP_KIND_AUDIO:
            audio_track_free((AT_AudioTrack*)slot->live);
            break;
        default:
            break;
        }
    }
    free(slot->live);
    free(slot->copy);
    memset(slot, 0, sizeof *slot);
}

// ─── The console's track list ────────────────────────────────────────────────

// Piano, layered keys, a track per --play / --sample file, the drumpad (--pad /
// --pads, or with no sound option at all the "sample" folder), the machine.
static uint32_t console_tracks(const AT_AppOptions* o, AT_AppTrackSpec* specs) {
    uint32_t n = 0;
    specs[n++] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_PIANO };
    specs[n++] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_LAYERS };
    for (uint32_t i = 0; i < o->sound_count && n < AT_TRACK_MAX; i++)
        specs[n++] = (AT_AppTrackSpec){ .kind = o->sound_is_sampler[i] ? AT_APP_KIND_SAMPLER : AT_APP_KIND_PLAYER,
                                        .file = o->sound_path[i], .cut = &o->sound_cut[i], .from_command_line = true };
    if (n < AT_TRACK_MAX && (o->pad_count || o->pad_folder || o->sound_count == 0)) {
        AT_AppTrackSpec* d = &specs[n++];
        *d = (AT_AppTrackSpec){ .kind = AT_APP_KIND_DRUMPAD, .pad_folder = o->pad_folder,
                                .default_kit = o->sound_count == 0 && o->pad_count == 0, .from_command_line = o->pad_count > 0 };
        for (uint32_t i = 0; i < o->pad_count; i++) {
            d->pads[i]     = o->pad_path[i];
            d->pad_cuts[i] = &o->pad_cut[i];
        }
        d->pad_count = o->pad_count;
    }
    if (n < AT_TRACK_MAX) specs[n++] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_MACHINE, .file = o->machine_path };
    if (n < AT_TRACK_MAX && o->audio_track) specs[n++] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_AUDIO };
    return n;
}

// ─── Setup / teardown ────────────────────────────────────────────────────────

void app_instruments_teardown(AT_App* app) {
    for (int i = 0; i < AT_TRACK_MAX; i++) free_slot(&s_slots[i]);
    app->target_count = 0;
    app->drumpad      = NULL;
    app->audio        = NULL;
    app->audio_track  = -1;
}

bool app_instruments_setup(AT_App* app, float sample_rate) {
    track_list_init(&app->tracks, &app->engine);
    app->drumpad           = NULL;
    app->audio             = NULL;
    app->audio_track       = -1;
    app->first_sound_track = -1;
    app->target_count      = 0;
    memset(app->track_info, 0, sizeof app->track_info);

    static AT_AppTrackSpec console[AT_TRACK_MAX];
    const AT_AppTrackSpec* specs = app->options.track_specs;
    uint32_t               count = app->options.track_spec_count;
    if (count == 0) {
        memset(console, 0, sizeof console);
        count = console_tracks(&app->options, console);
        specs = console;
    }
    if (count > AT_TRACK_MAX) count = AT_TRACK_MAX;

    for (uint32_t i = 0; i < count; i++) {
        const int result = add_track(app, sample_rate, &specs[i], (int)i);
        if (result == LEFT_OUT) { free_slot(&s_slots[i]); continue; }
        if (result == FAILED)   { app_instruments_teardown(app); return false; }
    }
    return true;
}

// ─── Track settings ──────────────────────────────────────────────────────────

void app_set_track_sync(AT_App* app, uint32_t track, bool on) {
    if (track >= app->tracks.count) return;
    engine_set_channel_sync(&app->engine, app->tracks.tracks[track].channel, on);
    app_record_note(app, "track %u sync %s", track + 1, on ? "on" : "off");
}

bool app_track_sync(const AT_App* app, uint32_t track) {
    return track < app->tracks.count && engine_channel_sync_sent(&app->engine, app->tracks.tracks[track].channel);
}

// P on a machine track: its sound and whole setup as machines\machine_NNN.atengine
// (the first free number), to load again with --machine or in AudioStudio.
void app_save_machine(AT_App* app) {
    const uint32_t t = app->tracks.selected;
    if (t >= app->tracks.count || app->track_info[t].kind != AT_APP_KIND_MACHINE) return;
    AT_Machine* m = (AT_Machine*)app->tracks.tracks[t].instrument.self;
    CreateDirectoryA("machines", NULL);                     // fails harmlessly if it exists
    char path[MAX_PATH] = "";
    for (int n = 1; n < 1000; n++) {
        snprintf(path, sizeof path, "machines\\machine_%03d.atengine", n);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) break;
    }
    char error[300] = "";
    if (machine_save_preset(m, path, AT_MACHINE_SAVE_ALL, error, sizeof error))
        printf("\nsaved %s: audiatron --machine %s loads it\n", path, path);
    else
        printf("\ncannot save the engine: %s\n", error);
}
