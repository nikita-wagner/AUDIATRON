// ═══════════════════════════════════════════════════════════════════════════
//  instruments/replay/replay.h — plays an input recording back, on time
//
//  Plays a recording: it reads one (recording/event_log) and hands every timed
//  line to its host at the right moment; the host (the app) knows what a line
//  means and plays it on its copies of the tracks: notes on the piano and
//  layer copies, songs on the player copies, pads on the drum tracks' copies,
//  track volume and effects on the copies' channels. It has no track of its
//  own: the console plays the last take with F8, AudioStudio plays the .txt
//  regions of its audio tracks with it (replay_play_part).
//
//  Timing: lines are handed over AT_REPLAY_LOOKAHEAD_MS before they are due,
//  each with its exact engine frame; the host sends it with engine_at(), so
//  the engine applies it on that frame (it splits the audio block there).
//  This runs on the control thread: nothing here touches the audio thread,
//  and a late control thread only shortens the lookahead.
//
//  Files in this folder:
//    replay.c          load, transport, handing lines to the host
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "instruments/instrument.h"
#include "io/sound_folder/sound_folder.h"
#include "recording/event_log/event_log.h"

#define AT_REPLAY_LOOKAHEAD_MS  250         // lines are sent this far ahead of their time
#define AT_REPLAY_START_MS      120         // a replay starts this long after P (its first lines must arrive first)
#define AT_REPLAY_LIST_MAX      256         // recordings the arrow keys step through

// What the replay asks of its host. All on the control thread; `at` is an
// engine frame, to be used with engine_at().
typedef struct {
    void*    user;
    uint64_t (*now)  (void* user);                          // engine frames rendered so far
    void     (*begin)(void* user, uint64_t at);             // a pass starts at `at`: put the copies back to silence
    void     (*apply)(void* user, uint64_t at, const char* line);   // do this line at `at`
    void     (*end)  (void* user, uint64_t at);             // stop at `at`; at = 0: stop now and drop what is waiting
    bool     (*load) (void* user, const char* path);        // load another recording (it calls replay_load); false = failed
} AT_ReplayHost;

typedef struct {
    AT_ReplayHost   host;
    AT_EventLogFile file;                   // the recording; setup lines first, then timed lines by time
    bool            loaded;
    uint32_t        first_timed;            // index of the first timed line in file.lines
    uint64_t        length;                 // frames, from the "end" line
    float           sample_rate;
    char            label[48];
    char            path[AT_SOUND_FOLDER_PATH];             // the loaded recording, as it was given

    // the recordings in its folder, for the arrow keys (control thread)
    char            folder[AT_SOUND_FOLDER_PATH];           // where to look while nothing is loaded
    char            list[AT_REPLAY_LIST_MAX][AT_SOUND_FOLDER_PATH];
    int             list_count;
    int             list_index;                             // the loaded one, -1 = not in the list
    char            tried[AT_SOUND_FOLDER_PATH];            // the last one the arrows picked (if it would not load)

    AT_Engine*      engine;                 // NULL until attach
    uint32_t        channel;

    // transport, control thread
    bool            playing;
    bool            loop;
    uint64_t        start;                  // engine frame where the pass begins (its time from_s)
    uint32_t        next;                   // next line to hand over
    bool            end_sent;
    double          from_s;                 // a part: the recording's time at `start` (0 = from its beginning)
    double          rate;                   // a part: its speed (2 = twice as fast); 1 normally
    uint64_t        until;                  // a part: engine frame where it stops (0 = at its end)
} AT_Replay;

void replay_init(AT_Replay* r, float sample_rate, AT_ReplayHost host);

// Reads a recording; stops a running replay first. false = failed, `error`
// says why (the replay is then empty). The setup lines ("pad 1 A file ...")
// are in r->file for the host to read.
bool replay_load  (AT_Replay* r, const char* path, char* error, size_t error_cap);
void replay_unload(AT_Replay* r);

// The folder the arrow keys look in while no recording is loaded (afterwards:
// the loaded recording's folder).
void replay_set_folder(AT_Replay* r, const char* folder);

// The previous (-1) / next (+1) recording in the folder, loaded through the
// host; a playing replay goes on with it. false = none there / it would not load.
bool replay_select(AT_Replay* r, int step);

void replay_play    (AT_Replay* r);         // from the start; on a synced track on the sync clock's next bar
// A part of the recording, for an arrangement: its time `from_s` sounds at engine
// frame `at`, played `rate` times as fast (event times scaled: a stretch), and it
// stops at engine frame `until` (0 = its end). Settings recorded before `from_s`
// are applied at `at`; its notes and hits there are left out.
void replay_play_part(AT_Replay* r, uint64_t at, double from_s, double rate, uint64_t until);
void replay_stop    (AT_Replay* r);
void replay_set_loop(AT_Replay* r, bool loop);

// Hands over every line due within the lookahead. Call it regularly (every
// control-loop tick, or before every block when rendering offline).
void replay_pump(AT_Replay* r);

double replay_position_seconds(const AT_Replay* r);
static inline bool replay_is_playing(const AT_Replay* r) { return r->playing; }


