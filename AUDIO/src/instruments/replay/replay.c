#include "instruments/replay/replay.h"

#include <stdio.h>
#include <string.h>

void replay_init(AT_Replay* r, float sample_rate, AT_ReplayHost host) {
    memset(r, 0, sizeof *r);
    r->sample_rate = sample_rate;
    r->host        = host;
    r->rate        = 1.0;
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
    if (r->engine && engine_channel_sync_sent(r->engine, r->channel))
        r->start = engine_sync_next_bar(r->engine, r->start);   // synced: on the clock's next bar
    r->next     = r->first_timed;
    r->end_sent = false;
    r->playing  = true;
    r->from_s   = 0.0;
    r->rate     = 1.0;
    r->until    = 0;
    r->host.begin(r->host.user, r->start);
    replay_pump(r);
}

void replay_play_part(AT_Replay* r, uint64_t at, double from_s, double rate, uint64_t until) {
    if (!r->loaded) return;
    if (r->playing && !r->end_sent) {
        if (r->until && r->until <= at) r->host.end(r->host.user, r->until);   // the pass before ends where it should
        else                            r->host.end(r->host.user, 0);          // it overlaps: it stops now
    }
    r->start    = at;
    r->from_s   = from_s > 0.0 ? from_s : 0.0;
    r->rate     = rate < 0.05 ? 0.05 : rate > 20.0 ? 20.0 : rate;
    r->until    = until;
    r->next     = r->first_timed;
    r->end_sent = false;
    r->playing  = true;
    r->loop     = false;
    r->host.begin(r->host.user, r->start);
    replay_pump(r);
}

// A line that makes something sound rather than sets something: left out
// when a part starts after it.
static bool is_event(const char* text) {
    static const char* const words[] = { " note ", " hit", " loop start", " sampler play", " sampler seek", "all notes off" };
    for (size_t i = 0; i < sizeof words / sizeof *words; i++)
        if (strstr(text, words[i])) return true;
    return false;
}

// The engine frame of a recording time in the pass being sent.
static uint64_t frame_of(const AT_Replay* r, double seconds) {
    const double rate = r->rate > 0.0 ? r->rate : 1.0;
    const double s    = seconds > r->from_s ? (seconds - r->from_s) / rate : 0.0;
    return r->start + to_frames(r, s);
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
            uint64_t          at   = frame_of(r, line->seconds);
            if (r->until && at >= r->until) { r->next = r->file.count; break; }   // the part ends here
            if (at > horizon) return;
            const bool before = line->seconds < r->from_s;
            if (strcmp(line->text, "end") && !(before && is_event(line->text))) r->host.apply(r->host.user, at, line->text);
            r->next++;
        }

        // all lines of this pass are out: the end
        uint64_t end = frame_of(r, (double)r->length / r->sample_rate);
        if (r->until && r->until < end) end = r->until;
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
    return r->from_s + (time > r->start ? (double)(time - r->start) / r->sample_rate * r->rate : 0.0);
}
