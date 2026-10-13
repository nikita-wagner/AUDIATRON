// ═══════════════════════════════════════════════════════════════════════════
//  recording/event_log/event_log.h — a timed list of what happened, as text
//
//  The file format of an input recording. Plain text, one thing per line, so
//  it can be read and edited in any editor:
//
//      # comment
//      rate 48000                      ← a line without a time: setup
//      pad 1 A file C:\kits\kick.wav
//      0.000000 pad 1 A pitch +0       ← "seconds what": an event
//      1.250000 key A down
//      1.250000 pad 1 A hit 1
//      1.480000 key A up
//      9.800000 end                    ← the length of the recording
//
//  Times are seconds since the recording started, written with 6 decimals
//  (a microsecond; one frame at 48 kHz is 21 µs), so they are rate-independent
//  and exact enough to place every event on its frame again.
//
//  This module only knows lines and times. What a line means ("key", "pad",
//  "track") is up to whoever writes and reads it.
//
//  Writing does file I/O: control thread only, never the audio thread.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ─── Writing ─────────────────────────────────────────────────────────────────

typedef struct {
    FILE*    file;
    uint64_t start_frame;                   // engine frame that is time 0
    double   rate;                          // engine frames per second
} AT_EventLog;

// false = the file cannot be created.
bool event_log_open  (AT_EventLog* log, const char* path, double rate, uint64_t start_frame);
// A line without a time (setup, comments with a leading "# ").
void event_log_header(AT_EventLog* log, const char* fmt, ...);
// "seconds text" for the event at engine frame `frame`.
void event_log_write (AT_EventLog* log, uint64_t frame, const char* fmt, ...);
// Writes the "end" line at `end_frame` and closes. Safe on a closed log.
void event_log_close (AT_EventLog* log, uint64_t end_frame);

double event_log_seconds(const AT_EventLog* log, uint64_t frame);

// ─── Reading ─────────────────────────────────────────────────────────────────

typedef struct {
    bool        timed;                      // false = a setup line
    double      seconds;                    // timed lines only
    const char* text;                       // the line without its time, trimmed
} AT_LogLine;

typedef struct {
    char*       buffer;                     // the whole file; lines point into it
    AT_LogLine* lines;                      // setup lines first (file order), then timed lines by time
    uint32_t    count;
    double      end_seconds;                // the "end" line, else the last event's time
} AT_EventLogFile;

// Reads a whole file. Comments and empty lines are skipped; timed lines are
// sorted by time (lines with equal times keep their order). false = failed,
// `error` says why. Free with event_log_file_free().
bool event_log_read     (const char* path, AT_EventLogFile* out, char* error, size_t error_cap);
void event_log_file_free(AT_EventLogFile* f);
