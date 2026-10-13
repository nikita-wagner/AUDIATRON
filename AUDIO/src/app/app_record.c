// ═══════════════════════════════════════════════════════════════════════════
//  app/app_record.c — recording: input (F5), WAV (F6), both together (F7)
//
//      F5   input recording on / off   → recordings\rec_001.txt
//      F6   WAV recording on / off     → recordings\rec_001.wav
//      F7   both on / off at once (one take number, one time 0)
//
//  The input recording is a text file (recording/event_log), each line with
//  its time:
//    - every key press and release
//    - what every instrument did, read at the engine (engine_set_observer):
//      notes ("track 1 note 60 on 0.9") and parameter changes of the synth,
//      layer stack, sampler and pitch shifter ("track 2 layers 2 mute 1",
//      "track 5 sampler rate 1.12"; names in app_params.c)
//    - every drumpad change (instruments/drumpad/drumpad_events.c: hits, loop
//      start / stop, pitch, tempo, volume, pan, effects, sync, cut)
//    - the app's own changes (track select, volume, mute, solo, hold, track
//      effects, master volume, all notes off)
//  It starts with what is on every track and the state of everything, so it is
//  a complete save, and the replay track can play it all back
//  (app_replay.c).
//
//  When an input recording stops, it is loaded into the replay track.
//
//  Times are the engine's frame count, read when the key arrives; the WAV
//  counts the same frames, so both share one time line. (A change reaches the
//  sound at the next audio block, so a logged time can be up to one device
//  period early: 10 ms in WASAPI shared mode, 3 ms exclusive.)
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdarg.h>
#include <stdio.h>

#include "input/key_names/key_names.h"

#define WAV_HOOK_SLOT  (AT_ENGINE_HOOK_SLOTS - 1)   // the last touch: the file gets what the device gets
#define FENCE_MS       2000

// ─── Time and the audio thread ───────────────────────────────────────────────

uint64_t app_now(const AT_App* app) {
    return engine_now(&app->engine);
}

static volatile LONG s_fence_seen;
static LONG          s_fence_sent;

static void fence_reached(void* self, int param, float value) {
    (void)value;
    InterlockedExchange((volatile LONG*)self, param);
}

bool app_engine_fence(AT_App* app) {
    // Nothing renders concurrently (offline, tests, shutting down): the next
    // render applies the queued commands before it uses anything.
    if (!app->audio_thread.handle) return true;

    LONG ticket = ++s_fence_sent;
    if (!engine_set_param(&app->engine, fence_reached, (void*)&s_fence_seen, (int)ticket, 0.0f)) return false;
    for (int waited = 0; waited < FENCE_MS; waited++) {
        if (InterlockedExchangeAdd(&s_fence_seen, 0) == ticket) return true;
        Sleep(1);
    }
    return false;
}

// ─── File names ──────────────────────────────────────────────────────────────

static const char* record_dir(const AT_App* app) {
    return app->options.record_dir ? app->options.record_dir : "recordings";
}

static bool file_exists(const char* path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void take_path(const AT_App* app, int take, const char* extension, char* out, size_t cap) {
    snprintf(out, cap, "%s\\rec_%03d.%s", record_dir(app), take, extension);
}

// The first number with neither file: a new take never overwrites an old one.
static int next_take(const AT_App* app) {
    char txt[260], wav[260];
    for (int take = 1; take < 1000; take++) {
        take_path(app, take, "txt", txt, sizeof txt);
        take_path(app, take, "wav", wav, sizeof wav);
        if (!file_exists(txt) && !file_exists(wav)) return take;
    }
    return 999;
}

// While one recording runs, the other one joins its take.
static int current_take(AT_App* app) {
    AT_AppRecord* r = &app->record;
    if (!r->input_on && !r->wav_on) {
        CreateDirectoryA(record_dir(app), NULL);            // fails harmlessly if it exists
        r->take = next_take(app);
    }
    return r->take;
}

// ─── Input recording ─────────────────────────────────────────────────────────

void app_record_note(AT_App* app, const char* fmt, ...) {
    AT_AppRecord* r = &app->record;
    if (!r->input_on) return;

    char text[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    // A change sent ahead for a frame (engine_at, e.g. the drumpad pattern's
    // hits) is logged at that frame, not at the moment it was sent.
    uint64_t at = app->engine.stamp_frame ? app->engine.stamp_frame : app_now(app);
    event_log_write(&r->log, at, "%s", text);
}

void app_record_key(AT_App* app, const AT_KeyEvent* ev) {
    if (!app->record.input_on || ev->repeat) return;

    // The arrows share their codes with the numpad; the extended flag tells
    // them apart. Arrows get plain English names, the rest Windows' names
    // (in the system's language, e.g. "1 (ZEHNERTASTATUR)" for numpad 1).
    char name[32];
    const char* arrow = !ev->extended          ? NULL
                      : ev->scan_code == AT_SC_UP    ? "Up"   : ev->scan_code == AT_SC_DOWN  ? "Down"
                      : ev->scan_code == AT_SC_LEFT  ? "Left" : ev->scan_code == AT_SC_RIGHT ? "Right" : NULL;
    if (arrow) snprintf(name, sizeof name, "%s", arrow);
    else       key_name_ex(ev->scan_code, ev->extended, name, sizeof name);
    for (char* c = name; *c; c++) if (*c == ' ') *c = '_';         // one word per key
    app_record_note(app, "key %s %s", name, ev->down ? "down" : "up");
}

// Drumpad changes while recording.
static void on_pad_event(void* user, const AT_PadEvent* ev) {
    AT_App* app = (AT_App*)user;
    char    text[96];
    drumpad_event_format(ev, text, sizeof text);
    app_record_note(app, "%s", text);
}

// Notes and parameter changes of the instruments, from the engine.
static void on_engine_event(void* user, const AT_EngineEvent* ev) {
    AT_App* app = (AT_App*)user;
    if (!app->record.input_on) return;

    if (ev->type == AT_ENGINE_EV_PARAM) {
        const AT_AppTarget* t = app_target_find_live(app, ev->fn, ev->self);
        if (!t) return;                                     // not a live track's (drumpad internals, the replay's copies)
        char text[96];
        app_param_format(t, ev->param, ev->value, text, sizeof text);
        app_record_note(app, "track %d %s", t->track + 1, text);
        return;
    }

    // notes: a live melodic track's channel (the drumpad has its own "pad" lines)
    int track = -1;
    for (uint32_t i = 0; i < app->tracks.count; i++)
        if ((int)app->tracks.tracks[i].channel == ev->channel) track = (int)i;
    if (track < 0) return;
    AT_AppKind kind = app->track_info[track].kind;
    if (kind == AT_APP_KIND_DRUMPAD || kind == AT_APP_KIND_REPLAY) return;

    switch (ev->type) {
    case AT_ENGINE_EV_NOTE_ON:       app_record_note(app, "track %d note %d on %.9g", track + 1, ev->note, ev->value); break;
    case AT_ENGINE_EV_NOTE_OFF:      app_record_note(app, "track %d note %d off", track + 1, ev->note);                break;
    case AT_ENGINE_EV_ALL_NOTES_OFF: app_record_note(app, "track %d all notes off", track + 1);                         break;
    default: break;
    }
}

// The state of a target at time 0: "track N <line>".
typedef struct { AT_App* app; int track; } SnapshotContext;

static void on_target_line(void* user, const char* line) {
    SnapshotContext* c = (SnapshotContext*)user;
    event_log_write(&c->app->record.log, c->app->record.log.start_frame, "track %d %s", c->track + 1, line);
}

// What every track is (setup lines) and how it is set (lines at time 0).
static void write_tracks(AT_App* app) {
    AT_EventLog* log = &app->record.log;
    for (uint32_t t = 0; t < app->tracks.count; t++) {
        const AT_AppTrackInfo* info = &app->track_info[t];
        if (info->file[0]) event_log_header(log, "track %u %s file %s", t + 1, app_kind_name(info->kind), info->file);
        else               event_log_header(log, "track %u %s", t + 1, app_kind_name(info->kind));
    }
}

static void write_track_state(AT_App* app) {
    AT_EventLog* log   = &app->record.log;
    uint64_t     start = log->start_frame;

    for (uint32_t t = 0; t < app->tracks.count; t++) {
        const AT_Track* track = &app->tracks.tracks[t];
        if (app->track_info[t].kind == AT_APP_KIND_REPLAY || app->track_info[t].kind == AT_APP_KIND_FUSION) continue;
        event_log_write(log, start, "track %u volume %.9g", t + 1, track->volume);
        event_log_write(log, start, "track %u pan %.9g", t + 1, track->pan);
        event_log_write(log, start, "track %u mute %s", t + 1, track->mute ? "on" : "off");
        event_log_write(log, start, "track %u solo %s", t + 1, track->solo ? "on" : "off");
        event_log_write(log, start, "track %u hold %s", t + 1, track->hold ? "on" : "off");
        event_log_write(log, start, "track %u echo_ms %d", t + 1, app_echo_ms(app, t));
        for (uint32_t fx = 0; fx < app->effect_count; fx++)
            if (app_effect_enabled(app, fx, t)) event_log_write(log, start, "track %u %s on", t + 1, app->effects[fx].name);
    }
    for (uint32_t i = 0; i < app->target_count; i++) {
        SnapshotContext c = { app, app->targets[i].track };
        app_target_snapshot(&app->targets[i], on_target_line, &c);
    }
}

// The drum state at time 0.
static void on_snapshot(void* user, const AT_PadEvent* ev) {
    AT_App* app = (AT_App*)user;
    char    text[96];
    drumpad_event_format(ev, text, sizeof text);
    event_log_write(&app->record.log, app->record.log.start_frame, "%s", text);
}

static void start_input(AT_App* app) {
    AT_AppRecord* r = &app->record;
    take_path(app, current_take(app), "txt", r->input_path, sizeof r->input_path);

    if (!event_log_open(&r->log, r->input_path, app->sample_rate, app_now(app))) {
        printf("\ncannot create %s\n", r->input_path);
        return;
    }

    event_log_header(&r->log, "# AUDIATRON input recording. One event per line: seconds since the start, then what happened.");
    event_log_header(&r->log, "# \"key\" lines are what was pressed; \"pad\" lines are what the drumpad did (the replay plays those).");
    event_log_header(&r->log, "audiatron recording 1");
    event_log_header(&r->log, "rate %u", (unsigned)app->sample_rate);
    // the engine frame at time 0: a replay runs its copies' LFOs in the same phase
    event_log_header(&r->log, "clock %.0f", (double)r->log.start_frame);
    write_tracks(app);

    AT_Drumpad* d = app->drumpad;
    if (d) {
        // full paths: the replay may run from another folder
        for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
            if (!d->pads[i].loaded) continue;
            char full[260], key[16];
            if (!GetFullPathNameA(d->pads[i].path, sizeof full, full, NULL)) snprintf(full, sizeof full, "%s", d->pads[i].path);
            key_name(drumpad_pad_key(i), key, sizeof key);
            event_log_header(&r->log, "pad %d %s file %s", i + 1, key, full);
        }
    }
    if (r->wav_on && r->wav.started)
        event_log_header(&r->log, "# %s was already recording; it started %.6f s before this file's time 0",
                         r->wav_path, (double)(r->log.start_frame - r->wav.first_frame) / app->sample_rate);

    r->input_on = true;
    write_track_state(app);
    if (d) {
        drumpad_snapshot(d, on_snapshot, app);
        drumpad_set_observer(d, on_pad_event, app);
    }
    engine_set_observer(&app->engine, on_engine_event, app);
    const AT_Track* t = track_list_selected(&app->tracks);
    if (t) event_log_write(&r->log, r->log.start_frame, "track %u select %s", app->tracks.selected + 1, t->instrument.name);
    printf("\nrecording input to %s\n", r->input_path);
}

static void stop_input(AT_App* app, bool load_into_replay) {
    AT_AppRecord* r = &app->record;
    if (!r->input_on) return;

    if (app->drumpad) drumpad_set_observer(app->drumpad, NULL, NULL);
    engine_set_observer(&app->engine, NULL, NULL);
    event_log_close(&r->log, app_now(app));
    r->input_on = false;
    printf("\nsaved %s\n", r->input_path);

    if (load_into_replay && app->replay && app_replay_load(app, r->input_path))
        printf("loaded into track %d (replay): Tab there, P plays it\n", app->replay_track + 1);
}

// ─── WAV recording ───────────────────────────────────────────────────────────

static void start_wav(AT_App* app) {
    AT_AppRecord* r = &app->record;
    take_path(app, current_take(app), "wav", r->wav_path, sizeof r->wav_path);

    if (!wav_recorder_open(&r->wav, r->wav_path, (uint32_t)app->sample_rate)) {
        printf("\ncannot create %s (or out of memory)\n", r->wav_path);
        return;
    }
    engine_set_hook(&app->engine, AT_HOOK_OUTPUT, 0, WAV_HOOK_SLOT, wav_recorder_process, &r->wav);
    r->wav_on           = true;
    r->wav_start_logged = false;
    printf("\nrecording audio to %s\n", r->wav_path);
}

static void stop_wav(AT_App* app) {
    AT_AppRecord* r = &app->record;
    if (!r->wav_on) return;

    engine_set_hook(&app->engine, AT_HOOK_OUTPUT, 0, WAV_HOOK_SLOT, NULL, NULL);
    r->wav_on = false;

    if (app_engine_fence(app)) {
        wav_recorder_close(&r->wav);                        // the rest, then the file; frees the ring
    } else {
        // The audio thread did not confirm it let go of the ring: keep the
        // ring (a leak is harmless, a freed ring in use is not), save the file.
        wav_recorder_flush(&r->wav);
        wav_writer_close(&r->wav.wav);
        printf("\n(audio thread did not answer; the recording buffer is kept)\n");
    }
    printf("\nsaved %s (%.1f s%s)\n", r->wav_path, wav_recorder_seconds(&r->wav),
           r->wav.dropped ? ", some audio was lost: the program was too busy to save it" : "");
}

// ─── Keys, tick, status, shutdown ────────────────────────────────────────────

bool app_record_hotkey(AT_App* app, uint16_t scan) {
    AT_AppRecord* r = &app->record;

    switch (scan) {
    case AT_SC_F5:
        if (r->input_on) stop_input(app, true); else start_input(app);
        return true;
    case AT_SC_F6:
        if (r->wav_on) stop_wav(app); else start_wav(app);
        return true;
    case AT_SC_F7:
        if (r->input_on || r->wav_on) {                     // stop whatever runs
            stop_wav(app);
            stop_input(app, true);
        } else {                                            // start both on the same frame
            start_wav(app);
            start_input(app);
        }
        return true;
    default:
        return false;
    }
}

void app_record_tick(AT_App* app) {
    AT_AppRecord* r = &app->record;
    if (!r->wav_on) return;

    wav_recorder_flush(&r->wav);

    // The WAV's first frame, exactly, once the audio thread has seen the hook.
    if (r->input_on && !r->wav_start_logged && r->wav.started) {
        event_log_write(&r->log, r->wav.first_frame, "wav start %s", r->wav_path);
        r->wav_start_logged = true;
    }
}

static void format_time(char* out, size_t cap, double seconds) {
    int s = (int)seconds;
    snprintf(out, cap, "%d:%02d", s / 60, s % 60);
}

void app_record_status(const AT_App* app, char* out, size_t cap) {
    const AT_AppRecord* r = &app->record;
    out[0] = '\0';
    if (!r->input_on && !r->wav_on) return;

    char in[16] = "", wav[16] = "";
    if (r->input_on) format_time(in,  sizeof in,  event_log_seconds(&r->log, app_now(app)));
    if (r->wav_on)   format_time(wav, sizeof wav, wav_recorder_seconds(&r->wav));
    snprintf(out, cap, "REC%s%s%s%s ", r->input_on ? " in " : "", in, r->wav_on ? " wav " : "", wav);
}

void app_record_shutdown(AT_App* app) {
    stop_wav(app);
    stop_input(app, false);                                 // closing: nothing to replay into
}
