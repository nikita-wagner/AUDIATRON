// ═══════════════════════════════════════════════════════════════════════════
//  app/app_replay.c — plays a recording on copies of the tracks
//
//  The replay (instruments/replay; no track of its own: F8 in the console, an
//  audio track's .txt regions in AudioStudio) reads a recording and hands
//  every line over shortly before it is due, with its exact engine frame. This
//  file says what a line means and plays it, on the copies, never on the live
//  tracks, so you can play along:
//
//      track 1 note 60 on 0.9         a note on the piano copy
//      track 2 layers 2 mute 1        a layer of the layered copy
//      track 5 sampler play           the player copy's song
//      track 1 volume 0.7             the copy's channel: volume, pan,
//      track 1 mute on / solo / hold     mute, solo, hold, crush / tremolo / echo
//      track 3 pad 2 S loop start     a pad of the copy of drum track 3
//      pad 2 S loop start             (older recordings) a pad of the first drum track's copy
//      all notes off                  everything of the replay stops
//
//  Every command is sent with engine_at(frame, tag), so the engine applies it
//  on its frame; the tag lets a stop drop what is still waiting.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "layers/layer_stack.h"
#include "sampler/sampler.h"
#include "synth/synth.h"

#define REPLAY_TAG 0x52504C59                   // marks the replay's timed commands

static AT_Replay  s_replay;                     // the app's one replay
static uint64_t   s_recorded_clock;             // the engine frame at the recording's time 0 ("clock" line)

// ─── Where things of the recording play ──────────────────────────────────────

// The copy of this program's drum track `live`, NULL = none.
static AT_Drumpad* drum_copy(const AT_App* app, int live) {
    if (live < 0 || live >= (int)app->tracks.count || app->track_info[live].kind != AT_APP_KIND_DRUMPAD) return NULL;
    return (AT_Drumpad*)app->track_info[live].copy;
}

// Where an older recording's "pad" lines (no track) play: the first drum track the recording maps to.
static AT_Drumpad* first_drum_copy(const AT_App* app) {
    for (int t = 0; t < AT_TRACK_MAX; t++) {
        AT_Drumpad* d = drum_copy(app, app->replay_map[t]);
        if (d) return d;
    }
    for (int live = 0; live < (int)app->tracks.count; live++)
        if (drum_copy(app, live)) return drum_copy(app, live);
    return NULL;
}

// The copy channel of this program's track `live` (melodic tracks only), -1 = none.
static int melodic_copy_channel(const AT_App* app, int live) {
    if (live < 0 || live >= (int)app->tracks.count || !app->track_info[live].has_copy) return -1;
    return (int)(app->tracks.tracks[live].channel + AT_TRACK_MAX);
}

// The channel that plays recording track `t`, -1 = nothing plays it.
static int copy_channel(const AT_App* app, int t) {
    if (t < 0 || t >= AT_TRACK_MAX) return -1;
    return melodic_copy_channel(app, app->replay_map[t]);
}

// Mute and solo work like on the tracks (tracks/track_list.c), among the copies.
static void send_audibility(AT_App* app) {
    bool any_solo = false;
    for (int t = 0; t < AT_TRACK_MAX; t++) any_solo |= app->replay_solo[t];

    for (int t = 0; t < AT_TRACK_MAX; t++) {
        int ch = copy_channel(app, t);
        if (ch < 0) continue;
        bool audible = !app->replay_mute[t] && (!any_solo || app->replay_solo[t]);
        engine_set_channel_mute(&app->engine, (uint32_t)ch, !audible);
    }
}

// Every copy silent: notes off, songs stopped, loops stopped.
static void silence(AT_App* app) {
    for (int live = 0; live < (int)app->tracks.count; live++) {
        int ch = melodic_copy_channel(app, live);
        if (ch >= 0) engine_all_notes_off(&app->engine, ch);
    }
    for (uint32_t i = 0; i < app->target_count; i++)
        if (app->targets[i].fn == sampler_set_param)
            engine_set_param(&app->engine, sampler_set_param, app->targets[i].copy, AT_SAMPLER_PARAM_STOP, 0.0f);
    for (int live = 0; live < (int)app->tracks.count; live++) {
        AT_Drumpad* d = drum_copy(app, live);
        for (int p = 0; d && p < AT_DRUMPAD_PADS; p++) drumpad_loop_stop(d, p);
    }
}

// ─── The host the replay track calls ─────────────────────────────────────────

static uint64_t host_now(void* user) { return app_now((AT_App*)user); }

static bool host_load(void* user, const char* path) { return app_replay_load((AT_App*)user, path); }

// A pass starts: the copies back to a clean state (the recording's first
// lines then set them as they were).
static void host_begin(void* user, uint64_t at) {
    AT_App* app = (AT_App*)user;
    engine_at(&app->engine, at, REPLAY_TAG);

    silence(app);
    for (int t = 0; t < AT_TRACK_MAX; t++) app->replay_mute[t] = app->replay_solo[t] = false;
    for (int live = 0; live < (int)app->tracks.count; live++) {
        int ch = melodic_copy_channel(app, live);
        if (ch < 0) continue;
        // the copy's time runs as the original's did: clock-locked LFOs in the same phase
        engine_set_channel_clock(&app->engine, (uint32_t)ch, at - s_recorded_clock);
        engine_set_channel_hold(&app->engine, (uint32_t)ch, false);
        for (uint32_t fx = 0; fx < app->effect_count; fx++)
            if (app->effects[fx].enabled[ch]) app_effect_set_channel(app, fx, (uint32_t)ch, false);
    }
    send_audibility(app);
    for (int live = 0; live < (int)app->tracks.count; live++) {
        AT_Drumpad* d = drum_copy(app, live);
        for (int p = 0; d && p < AT_DRUMPAD_PADS; p++) {
            if (!d->pads[p].loaded) continue;
            drumpad_reset(d, p);
            drumpad_set_loop(d, p, false);
            drumpad_set_sync(d, p, true);
            drumpad_set_cut(d, p, 0.0f, 1.0f);
            drumpad_set_timing(d, p, 0.0f, 0.0f);
        }
    }
    engine_at(&app->engine, 0, 0);
}

static void host_end(void* user, uint64_t at) {
    AT_App* app = (AT_App*)user;
    if (at == 0) engine_cancel(&app->engine, REPLAY_TAG);   // stop now: drop what still waits
    engine_at(&app->engine, at, at ? REPLAY_TAG : 0);
    silence(app);
    engine_at(&app->engine, 0, 0);
}

static bool on_off(const char* word, bool* on) {
    if (!strcmp(word, "on"))  { *on = true;  return true; }
    if (!strcmp(word, "off")) { *on = false; return true; }
    return false;
}

// "track N ..." on the copy of track N.
static void apply_track_line(AT_App* app, const char* text) {
    int  t = 0, used = 0;
    char word[32] = "";
    if (sscanf(text, "track %d %31s%n", &t, word, &used) != 2) return;
    t--;
    int ch = copy_channel(app, t);
    if (ch < 0) return;
    int live = app->replay_map[t];                          // whose copy (synth, layers, sampler, ...)
    const char* rest = text + used;
    bool        on;
    char        arg[32] = "";
    sscanf(rest, " %31s", arg);

    if (!strcmp(word, "pad")) {                             // "track 3 pad 2 S hit 0.8": the drum track's copy
        AT_Drumpad* d = drum_copy(app, live);
        AT_PadEvent ev;
        if (d && drumpad_event_parse(text + used - 3, &ev) && d->pads[ev.pad].loaded) drumpad_event_apply(d, &ev);
        return;
    }
    if (!strcmp(word, "note")) {
        int   note = 0;
        float velocity = 1.0f;
        char  state[8] = "";
        if (sscanf(rest, " %d %7s %f", &note, state, &velocity) < 2) return;
        if (!strcmp(state, "on"))  engine_note_on (&app->engine, (uint32_t)ch, note, velocity);
        if (!strcmp(state, "off")) engine_note_off(&app->engine, (uint32_t)ch, note);
        return;
    }
    if (!strcmp(word, "all") && !strcmp(arg, "notes")) { engine_all_notes_off(&app->engine, ch); return; }
    if (!strcmp(word, "volume")) { engine_set_channel_volume(&app->engine, (uint32_t)ch, (float)atof(arg)); return; }
    if (!strcmp(word, "pan"))    { engine_set_channel_pan   (&app->engine, (uint32_t)ch, (float)atof(arg)); return; }
    if (!strcmp(word, "mute") && on_off(arg, &on)) { app->replay_mute[t] = on; send_audibility(app); return; }
    if (!strcmp(word, "solo") && on_off(arg, &on)) { app->replay_solo[t] = on; send_audibility(app); return; }
    if (!strcmp(word, "hold") && on_off(arg, &on)) { engine_set_channel_hold(&app->engine, (uint32_t)ch, on); return; }
    if (!strcmp(word, "echo_ms"))                  { app_set_echo_ms_channel(app, (uint32_t)ch, atoi(arg)); return; }

    int   param = 0;
    float value = 0.0f;
    int fx = app_effect_find(app, word);                    // crush / tremolo / echo
    if (fx >= 0 && on_off(arg, &on)) { app_effect_set_channel(app, (uint32_t)fx, (uint32_t)ch, on); return; }
    if (fx >= 0) {                                          // "track 2 tremolo rate 4.5": the copy's instance
        AT_ParamFn fn = app->effects[fx].set_param;
        void*      st = fn && app_object_param_parse(fn, rest, &param, &value) ? app_effect_state_channel(app, (uint32_t)fx, (uint32_t)ch) : NULL;
        if (st) engine_set_param(&app->engine, fn, st, param, value);
        return;
    }

    const AT_AppTarget* target = app_target_find_name(app, live, word);  // synth / layers / sampler / shifter

    // "track 2 layers 2 tremolo depth 0.3", "track 2 layers 3 synth attack 0.01": inside a layer of the copy
    if (target && target->fn == layer_stack_set_param) {
        int  layer = 0, n = 0;
        char kind[32] = "";
        if (sscanf(rest, " %d %31s%n", &layer, kind, &n) == 2 && layer >= 1) {
            AT_LayerStack* copy = (AT_LayerStack*)target->copy;
            AT_Layer*      l    = copy && (uint32_t)layer <= copy->count ? &copy->layers[layer - 1] : NULL;
            AT_ParamFn     fn   = NULL;
            void*          obj  = NULL;
            if (l && !strcmp(kind, "synth") && l->type == &AT_SYNTH_SOURCE) { fn = synth_set_param; obj = l->self; }
            for (int s = 0; l && !fn && s < AT_LAYER_INSERTS; s++) {
                AT_ParamFn f = app_effect_param_fn(l->inserts[s].fn);
                if (f && !strcmp(kind, app_object_kind(f))) { fn = f; obj = l->inserts[s].user; }
            }
            if (fn) {
                if (obj && app_object_param_parse(fn, rest + n, &param, &value))
                    engine_set_param(&app->engine, fn, obj, param, value);
                return;
            }
        }
    }
    if (target && app_param_parse(target, rest, &param, &value))
        engine_set_param(&app->engine, target->fn, target->copy, param, value);
    // anything else ("select", ...) is there to read
}

static void host_apply(void* user, uint64_t at, const char* line) {
    AT_App* app = (AT_App*)user;
    engine_at(&app->engine, at, REPLAY_TAG);

    AT_PadEvent pad;
    AT_Drumpad* d;
    if (!strncmp(line, "track ", 6))           apply_track_line(app, line);
    else if (!strcmp(line, "all notes off"))   silence(app);
    else if (drumpad_event_parse(line, &pad) && (d = first_drum_copy(app)) != NULL && d->pads[pad.pad].loaded)
        drumpad_event_apply(d, &pad);           // an older recording's pad

    engine_at(&app->engine, 0, 0);
}

// ─── Setup, loading ──────────────────────────────────────────────────────────

void app_replay_setup(AT_App* app) {
    replay_init(&s_replay, app->sample_rate, (AT_ReplayHost){ app, host_now, host_begin, host_apply, host_end, host_load });
    s_replay.engine  = &app->engine;                         // no channel of its own: it plays on the copies
    s_replay.channel = (uint32_t)AT_ENGINE_CHANNELS;
    replay_set_folder(&s_replay, app->options.record_dir ? app->options.record_dir : "recordings");
    app->replay = &s_replay;
}

void app_replay_teardown(AT_App* app) {
    s_replay.playing = false;                               // the engine is gone: nothing more is sent to it
    replay_unload(&s_replay);
    app->replay = NULL;
}

const char* app_kind_name(AT_AppKind kind) {
    switch (kind) {
    case AT_APP_KIND_PIANO:   return "piano";
    case AT_APP_KIND_LAYERS:  return "layers";
    case AT_APP_KIND_PLAYER:  return "player";
    case AT_APP_KIND_SAMPLER: return "sampler";
    case AT_APP_KIND_DRUMPAD: return "drumpad";
    case AT_APP_KIND_MACHINE: return "machine";
    case AT_APP_KIND_AUDIO:   return "audio";
    }
    return "?";
}

// Does this program's track `live` play what the recording's "<kind> [file]" track did?
static bool matches(const AT_App* app, int live, const char* kind, const char* file) {
    if (live < 0 || live >= (int)app->tracks.count) return false;
    const AT_AppTrackInfo* info = &app->track_info[live];
    if (strcmp(kind, app_kind_name(info->kind)) || !info->has_copy) return false;
    return !file || _stricmp(file, info->file) == 0;
}

// "track 5 player file C:\song.mp3": which of this program's tracks plays it?
// The same number if it matches, else the first unused track of the same kind
// (and file), so a recording still plays when the tracks are in another order.
static void map_track(AT_App* app, const char* line, bool used[AT_TRACK_MAX]) {
    int  t = 0, n = 0;
    char kind[32] = "";
    if (sscanf(line, "track %d %31s%n", &t, kind, &n) != 2 || t < 1 || t > AT_TRACK_MAX) return;
    t--;

    const char* file = strstr(line + n, "file ");
    if (file) file += 5;

    app->replay_map[t] = -1;
    if (!strcmp(kind, "replay") || !strcmp(kind, "fusion") || !strcmp(kind, "audio")) return;  // nothing recorded to replay

    int live = matches(app, t, kind, file) && !used[t] ? t : -1;
    for (int i = 0; live < 0 && i < (int)app->tracks.count; i++)
        if (!used[i] && matches(app, i, kind, file)) live = i;

    if (live < 0) {
        printf("the recording's track %d (%s%s%s) has no matching track here: its part is not replayed\n",
               t + 1, kind, file ? " " : "", file ? file : "");
        return;
    }
    used[live]         = true;
    app->replay_map[t] = live;
    if (live != t) printf("the recording's track %d (%s) plays on track %d here\n", t + 1, kind, live + 1);
}

bool app_replay_load(AT_App* app, const char* path) {
    AT_Replay* r = app->replay;
    if (!r) return false;

    replay_stop(r);

    char error[200] = "";
    bool ok = replay_load(r, path, error, sizeof error);
    if (!ok) printf("\ncannot replay %s: %s\n", path, error);

    // Without "track" lines (a hand-written file), track N is track N.
    bool used[AT_TRACK_MAX] = { false };
    for (int t = 0; t < AT_TRACK_MAX; t++) app->replay_map[t] = t;
    s_recorded_clock = 0;
    for (uint32_t i = 0; ok && i < r->file.count; i++) {
        const AT_LogLine* line = &r->file.lines[i];
        if (line->timed) break;

        if (!strncmp(line->text, "clock ", 6)) { s_recorded_clock = (uint64_t)strtod(line->text + 6, NULL); continue; }

        if (!strncmp(line->text, "track ", 6)) { map_track(app, line->text, used); continue; }
        // older recordings name their pad files ("pad 2 S file ..."): the drum tracks' own files play
    }
    return ok;
}

// ─── Background work, every tick / block ─────────────────────────────────────

void app_background_tick(AT_App* app) {
    app_record_tick(app);
    if (app->replay && app->replay->host.now) replay_pump(app->replay);
    for (uint32_t t = 0; t < app->tracks.count; t++) {      // instruments that play by themselves
        const AT_Instrument* in = &app->tracks.tracks[t].instrument;
        if (in->tick) in->tick(in->self);
    }
}
