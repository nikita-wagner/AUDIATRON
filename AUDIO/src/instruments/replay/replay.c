#include "instruments/replay/replay.h"

#include <stdio.h>
#include <string.h>

void replay_init(AT_Replay* r, float sample_rate, AT_ReplayHost host) {
    memset(r, 0, sizeof *r);
    r->sample_rate = sample_rate;
    r->host        = host;
}

void replay_unload(AT_Replay* r) {
    if (r->playing) replay_stop(r);
    event_log_file_free(&r->file);
    r->path[0]     = '\0';
    r->loaded      = false;
    r->length      = 0;
    r->first_timed = 0;
    r->label[0]    = '\0';
}

// ─── The recordings in the folder ────────────────────────────────────────────

static const char* file_name(const char* path) {
    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    return name;
}

// "recordings\rec_002.txt" → "recordings"; no folder → "."
static void folder_of(const char* path, char* out, size_t cap) {
    const char* name = file_name(path);
    if (name == path) { snprintf(out, cap, "."); return; }
    snprintf(out, cap, "%.*s", (int)(name - path - 1), path);
}

void replay_set_folder(AT_Replay* r, const char* folder) {
    snprintf(r->folder, sizeof r->folder, "%s", folder);
}

// Lists the .txt files next to the loaded recording (or the one the arrows
// last picked, or in r->folder), and finds where the loaded one is.
static void rescan(AT_Replay* r) {
    const char* current = r->loaded ? r->path : r->tried;
    char        folder[AT_SOUND_FOLDER_PATH];
    if (current[0]) folder_of(current, folder, sizeof folder);
    else            snprintf(folder, sizeof folder, "%s", r->folder[0] ? r->folder : ".");

    r->list_count = (int)sound_folder_list_ext(folder, ".txt", r->list, AT_REPLAY_LIST_MAX);
    r->list_index = -1;
    for (int i = 0; current[0] && i < r->list_count; i++)
        if (_stricmp(file_name(r->list[i]), file_name(current)) == 0) r->list_index = i;
}

bool replay_select(AT_Replay* r, int step) {
    bool was_playing = r->playing;
    rescan(r);
    if (r->list_count == 0 || !r->host.load) return false;

    int i = r->list_index < 0 ? (step > 0 ? 0 : r->list_count - 1)
                              : ((r->list_index + step) % r->list_count + r->list_count) % r->list_count;
    char path[AT_SOUND_FOLDER_PATH];
    snprintf(path, sizeof path, "%s", r->list[i]);          // loading lists the folder again
    snprintf(r->tried, sizeof r->tried, "%s", path);

    if (!r->host.load(r->host.user, path)) { rescan(r); return false; }
    if (was_playing) replay_play(r);
    return true;
}

// ─── Loading ─────────────────────────────────────────────────────────────────

static uint64_t to_frames(const AT_Replay* r, double seconds) {
    return (uint64_t)(seconds * (double)r->sample_rate + 0.5);
}

bool replay_load(AT_Replay* r, const char* path, char* error, size_t error_cap) {
    replay_unload(r);
    if (!event_log_read(path, &r->file, error, error_cap)) return false;

    r->first_timed = 0;
    while (r->first_timed < r->file.count && !r->file.lines[r->first_timed].timed) r->first_timed++;
    r->length = to_frames(r, r->file.end_seconds);

    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(r->label, sizeof r->label, "%s", name);
    snprintf(r->path,  sizeof r->path,  "%s", path);
    r->loaded = true;
    rescan(r);
    return true;
}

// ─── Transport ───────────────────────────────────────────────────────────────

static uint64_t now(const AT_Replay* r) { return r->host.now(r->host.user); }

static uint64_t lookahead(const AT_Replay* r) {
    return (uint64_t)(r->sample_rate * AT_REPLAY_LOOKAHEAD_MS / 1000.0f);
}

void replay_play(AT_Replay* r) {
    if (!r->loaded) return;
    if (r->playing) r->host.end(r->host.user, 0);         // a restart: silence what still waits

    r->start    = now(r) + (uint64_t)(r->sample_rate * AT_REPLAY_START_MS / 1000.0f);
    r->next     = r->first_timed;
    r->end_sent = false;
    r->playing  = true;
    r->host.begin(r->host.user, r->start);
    replay_pump(r);
}

void replay_stop(AT_Replay* r) {
    if (!r->playing) return;
    r->playing = false;
    r->host.end(r->host.user, 0);
}

void replay_set_loop(AT_Replay* r, bool loop) {
    r->loop = loop;
}

void replay_pump(AT_Replay* r) {
    if (!r->playing) return;

    const uint64_t time    = now(r);
    const uint64_t horizon = time + lookahead(r);

    for (;;) {
        // every line due before the horizon, with its exact frame
        while (r->next < r->file.count) {
            const AT_LogLine* line = &r->file.lines[r->next];
            uint64_t          at   = r->start + to_frames(r, line->seconds);
            if (at > horizon) return;
            if (strcmp(line->text, "end")) r->host.apply(r->host.user, at, line->text);
            r->next++;
        }

        // all lines of this pass are out: the end
        uint64_t end = r->start + r->length;
        if (r->loop && r->length > 0) {
            if (end > horizon) return;
            r->start = end;                                 // the next pass follows seamlessly
            r->next  = r->first_timed;
            r->host.begin(r->host.user, r->start);
            continue;
        }
        if (!r->end_sent) {
            r->host.end(r->host.user, end);
            r->end_sent = true;
        }
        if (time >= end) r->playing = false;
        return;
    }
}

double replay_position_seconds(const AT_Replay* r) {
    if (!r->playing) return 0.0;
    uint64_t time = now(r);
    return time > r->start ? (double)(time - r->start) / r->sample_rate : 0.0;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void replay_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_Replay* r = (AT_Replay*)self;
    r->engine  = engine;
    r->channel = channel;
    // The host plays the recording's drums on this channel: the drumpad's EQ.
    engine_set_eq_band(engine, (int)channel, 0, AT_FILTER_HIGHPASS, 25.0f, 0.707f, 0.0f);
}

static bool replay_key_adapter(void* self, const AT_KeyEvent* ev) {
    return replay_key((AT_Replay*)self, ev);
}

static void replay_release_all(void* self) {
    (void)self;                 // nothing is held: a replay keeps playing when another track is selected
}

static void replay_demo_start(void* self) {
    replay_play((AT_Replay*)self);
}

static void format_time(char* out, size_t cap, double seconds) {
    int s = (int)(seconds + 0.5);
    snprintf(out, cap, "%d:%02d", s / 60, s % 60);
}

// e.g. "rec_001.txt 0:12/0:45 play loop"
static void replay_status(void* self, char* out, size_t cap) {
    const AT_Replay* r = (const AT_Replay*)self;
    if (!r->loaded) { snprintf(out, cap, "empty (F5 / F7 records; Left / Right picks a recording)"); return; }

    char here[16], total[16], which[16] = "";
    format_time(here,  sizeof here,  replay_position_seconds(r));
    format_time(total, sizeof total, (double)r->length / r->sample_rate);
    if (r->list_index >= 0) snprintf(which, sizeof which, "%d/%d ", r->list_index + 1, r->list_count);
    snprintf(out, cap, "%.20s %s%s/%s %s%s", r->label, which, here, total,
             r->playing ? "play" : "stop", r->loop ? " loop" : "");
}

AT_Instrument replay_instrument(AT_Replay* r) {
    return (AT_Instrument){
        .name        = "replay",
        .help        = REPLAY_HELP,
        .self        = r,
        .attach      = replay_attach,
        .key         = replay_key_adapter,
        .release_all = replay_release_all,
        .status      = replay_status,
        .demo_start  = replay_demo_start
    };
}
