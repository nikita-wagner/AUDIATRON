#include "recording/event_log/event_log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// ─── Writing ─────────────────────────────────────────────────────────────────

bool event_log_open(AT_EventLog* log, const char* path, double rate, uint64_t start_frame) {
    memset(log, 0, sizeof *log);
    log->file = fopen(path, "w");
    if (!log->file) return false;
    log->rate        = rate > 0.0 ? rate : 48000.0;
    log->start_frame = start_frame;
    return true;
}

void event_log_header(AT_EventLog* log, const char* fmt, ...) {
    if (!log->file) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(log->file, fmt, args);
    va_end(args);
    fputc('\n', log->file);
}

double event_log_seconds(const AT_EventLog* log, uint64_t frame) {
    // an event stamped just before the start (a key read a block early) is at 0
    return frame > log->start_frame ? (double)(frame - log->start_frame) / log->rate : 0.0;
}

void event_log_write(AT_EventLog* log, uint64_t frame, const char* fmt, ...) {
    if (!log->file) return;
    fprintf(log->file, "%.6f ", event_log_seconds(log, frame));
    va_list args;
    va_start(args, fmt);
    vfprintf(log->file, fmt, args);
    va_end(args);
    fputc('\n', log->file);
    fflush(log->file);                      // a crash or a closed console keeps what was played
}

void event_log_close(AT_EventLog* log, uint64_t end_frame) {
    if (!log->file) return;
    event_log_write(log, end_frame, "end");
    fclose(log->file);
    log->file = NULL;
}

// ─── Reading ─────────────────────────────────────────────────────────────────

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) *--end = '\0';
    return s;
}

// Setup lines before timed ones; timed ones by time, ties in file order.
static int compare_lines(const void* a, const void* b) {
    const AT_LogLine* x = (const AT_LogLine*)a;
    const AT_LogLine* y = (const AT_LogLine*)b;
    if (x->timed != y->timed) return x->timed ? 1 : -1;
    if (x->timed && x->seconds != y->seconds) return x->seconds < y->seconds ? -1 : 1;
    return x->text < y->text ? -1 : x->text > y->text;     // pointers into the buffer = file order
}

static void set_error(char* error, size_t cap, const char* message, const char* path) {
    if (error && cap) snprintf(error, cap, "%s %s", message, path);
}

bool event_log_read(const char* path, AT_EventLogFile* out, char* error, size_t error_cap) {
    memset(out, 0, sizeof *out);

    FILE* f = fopen(path, "rb");
    if (!f) { set_error(error, error_cap, "cannot open", path); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); set_error(error, error_cap, "cannot read", path); return false; }

    out->buffer = (char*)malloc((size_t)size + 1);
    if (!out->buffer) { fclose(f); set_error(error, error_cap, "out of memory reading", path); return false; }
    size_t got = fread(out->buffer, 1, (size_t)size, f);
    fclose(f);
    out->buffer[got] = '\0';

    uint32_t capacity = 1;
    for (size_t i = 0; i < got; i++) capacity += out->buffer[i] == '\n';
    out->lines = (AT_LogLine*)calloc(capacity, sizeof *out->lines);
    if (!out->lines) { event_log_file_free(out); set_error(error, error_cap, "out of memory reading", path); return false; }

    double last = 0.0;
    bool   has_end = false;
    char*  line = out->buffer;
    while (line && *line) {
        char* next = strchr(line, '\n');
        if (next) *next++ = '\0';

        char* text = trim(line);
        line = next;
        if (!*text || *text == '#') continue;

        AT_LogLine* l   = &out->lines[out->count];
        char*       end = NULL;
        double      t   = strtod(text, &end);
        if (end != text && (*end == ' ' || *end == '\t' || *end == '\0')) {
            l->timed   = true;
            l->seconds = t < 0.0 ? 0.0 : t;
            l->text    = trim(end);
            if (!strcmp(l->text, "end")) { out->end_seconds = l->seconds; has_end = true; }
            if (l->seconds > last) last = l->seconds;
        } else {
            l->text = text;
        }
        out->count++;
    }
    if (!has_end) out->end_seconds = last;

    qsort(out->lines, out->count, sizeof *out->lines, compare_lines);
    return true;
}

void event_log_file_free(AT_EventLogFile* f) {
    free(f->lines);
    free(f->buffer);
    memset(f, 0, sizeof *f);
}
