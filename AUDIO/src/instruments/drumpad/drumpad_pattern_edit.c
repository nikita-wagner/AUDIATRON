// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_pattern_edit.c — "*": typing the pattern
//
//  The first "*" (no pattern yet):
//      * pattern size rows x steps (now 4x4): 4x4_          → Enter
//      * step time: ms or s (now 1000): 500_                → Enter (or "0.5s")
//      * row 1/4  A kick.wav (now 0000): 1010_              → Enter, one row per pad
//      ...                                                  the last Enter starts it
//  Once a pattern exists, "*" starts at the step time (type "8x4" there for a
//  new size) and then shows every row to fill again.
//
//  Size: "4x4" (also 4*4, 4,4); empty Enter keeps it. Step time: a number of
//  ms, or seconds with "s"; plain numbers below 10 are seconds too ("0.5");
//  empty Enter keeps it; 0 stops the pattern and closes. A row: 0 and 1, one
//  per step (missing ones are 0); empty Enter keeps the row. Backspace deletes,
//  Esc cancels everything. While it is open it takes every key.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input/key_names/key_names.h"

enum { STAGE_SIZE, STAGE_TIME, STAGE_ROW };                // rows: STAGE_ROW + r

#define STEP_MS_MIN   20.0f
#define STEP_MS_MAX   60000.0f

// ─── Showing the grid ────────────────────────────────────────────────────────

static void row_text(const AT_DrumPattern* p, int r, char* out) {
    for (int s = 0; s < p->steps; s++) out[s] = (p->bits[r] & (1u << s)) ? '1' : '0';
    out[p->steps] = '\0';
}

static void print_grid(const AT_Drumpad* d, const AT_DrumPattern* p, const char* title) {
    printf("\n%s: %d x %d, %.0f ms per step\n", title, p->rows, p->steps, p->step_ms);
    for (int r = 0; r < p->rows; r++) {
        char key[16], bits[AT_PATTERN_MAX_STEPS + 1];
        key_name(drumpad_pad_key(r), key, sizeof key);
        row_text(p, r, bits);
        printf("  %2d %-3s %-16.16s %s\n", r + 1, key, d->pads[r].loaded ? d->pads[r].label : "(no sound)", bits);
    }
}

// ─── Opening, closing ────────────────────────────────────────────────────────

static void set_stage(AT_Drumpad* d, int stage) {
    d->pat_stage   = stage;
    d->pat_text[0] = '\0';
    d->pat_note[0] = '\0';
}

void drumpad_pattern_edit_open(AT_Drumpad* d) {
    d->pat_draft = d->pattern;
    if (d->pattern.rows == 0) {                             // a first pattern: 4 x 4, one step a second
        memset(&d->pat_draft, 0, sizeof d->pat_draft);
        d->pat_draft.rows    = 4;
        d->pat_draft.steps   = 4;
        d->pat_draft.step_ms = 1000.0f;
        set_stage(d, STAGE_SIZE);
    } else {
        set_stage(d, STAGE_TIME);
    }
}

static void close_editor(AT_Drumpad* d) {
    set_stage(d, -1);
}

// ─── Enter ───────────────────────────────────────────────────────────────────

static bool is_size(const char* text) {
    return strchr(text, 'x') || strchr(text, 'X') || strchr(text, '*') || strchr(text, ',');
}

// "4x8" → the draft gets that size (rows that remain keep their bits).
static bool enter_size(AT_Drumpad* d, const char* text) {
    if (!text[0]) return true;                              // keep
    char* end;
    long rows = strtol(text, &end, 10);
    if (*end != 'x' && *end != 'X' && *end != '*' && *end != ',') { snprintf(d->pat_note, sizeof d->pat_note, "type rows x steps, e.g. 4x4"); return false; }
    long steps = strtol(end + 1, &end, 10);
    if (*end || rows < 1 || rows > AT_DRUMPAD_PADS || steps < 1 || steps > AT_PATTERN_MAX_STEPS) {
        snprintf(d->pat_note, sizeof d->pat_note, "1..%d rows x 1..%d steps", AT_DRUMPAD_PADS, AT_PATTERN_MAX_STEPS);
        return false;
    }
    AT_DrumPattern* p = &d->pat_draft;
    uint32_t mask = steps == 32 ? 0xFFFFFFFFu : (1u << steps) - 1u;
    for (int r = 0; r < AT_DRUMPAD_PADS; r++) p->bits[r] = r < rows ? p->bits[r] & mask : 0;
    p->rows  = (int)rows;
    p->steps = (int)steps;
    return true;
}

// false = stay (the note says why); true = go on. *stop = "0" was typed.
static bool enter_time(AT_Drumpad* d, const char* text, bool* stop) {
    *stop = false;
    if (!text[0]) return true;                              // keep
    char*  end;
    double v = strtod(text, &end);
    bool   seconds = (*end == 's' || *end == 'S') || (v > 0.0 && v < 10.0);
    if (*end == 's' || *end == 'S') end++;
    if (*end) { snprintf(d->pat_note, sizeof d->pat_note, "a number: 500 (ms) or 0.5s"); return false; }
    if (v == 0.0) { *stop = true; return true; }
    float ms = (float)(seconds ? v * 1000.0 : v);
    if (ms < STEP_MS_MIN || ms > STEP_MS_MAX) {
        snprintf(d->pat_note, sizeof d->pat_note, "%.0f .. %.0f ms", STEP_MS_MIN, STEP_MS_MAX);
        return false;
    }
    d->pat_draft.step_ms = ms;
    return true;
}

static void enter(AT_Drumpad* d) {
    const char* text = d->pat_text;

    if (d->pat_stage == STAGE_SIZE) {
        if (enter_size(d, text)) set_stage(d, STAGE_TIME);
        return;
    }

    if (d->pat_stage == STAGE_TIME) {
        if (is_size(text)) {                                // a new size, then the time again
            if (enter_size(d, text)) set_stage(d, STAGE_TIME);
            return;
        }
        bool stop;
        if (!enter_time(d, text, &stop)) return;
        if (stop) { drumpad_pattern_stop(d); close_editor(d); return; }
        set_stage(d, STAGE_ROW);
        print_grid(d, &d->pat_draft, "pattern (type each row: 1 = play, 0 = rest)");
        return;
    }

    // a row: 0 / 1 per step, missing ones are 0; empty keeps it
    int r = d->pat_stage - STAGE_ROW;
    if (text[0]) {
        uint32_t bits = 0;
        for (int s = 0; text[s]; s++) if (text[s] == '1') bits |= 1u << s;
        d->pat_draft.bits[r] = bits;
    }
    if (r + 1 < d->pat_draft.rows) { set_stage(d, STAGE_ROW + r + 1); return; }

    // the last row: it becomes the pattern and plays
    drumpad_pattern_set(d, d->pat_draft.rows, d->pat_draft.steps, d->pat_draft.bits, d->pat_draft.step_ms);
    drumpad_pattern_play(d);
    print_grid(d, &d->pattern, "pattern playing (* changes it, Space stops)");
    close_editor(d);
}

// ─── Keys ────────────────────────────────────────────────────────────────────

static bool allowed(const AT_Drumpad* d, char c) {
    if (d->pat_stage == STAGE_SIZE) return (c >= '0' && c <= '9') || c == 'x' || c == 'X' || c == '*' || c == ',';
    if (d->pat_stage == STAGE_TIME) return (c >= '0' && c <= '9') || c == '.' || c == 's' || c == 'S' || c == 'x' || c == 'X' || c == '*' || c == ',';
    return c == '0' || c == '1';
}

bool drumpad_pattern_edit_key(AT_Drumpad* d, const AT_KeyEvent* ev) {
    if (!ev->down) {                                        // letting go of a pad still counts
        for (int i = 0; i < AT_DRUMPAD_PADS; i++)
            if (drumpad_pad_key(i) == ev->scan_code && !ev->extended) d->pads[i].held = false;
        return true;
    }

    size_t len = strlen(d->pat_text);
    size_t max = d->pat_stage >= STAGE_ROW ? (size_t)d->pat_draft.steps : sizeof d->pat_text - 1;

    if (ev->scan_code == AT_SC_ESC && !ev->repeat)          close_editor(d);
    else if (ev->scan_code == AT_SC_ENTER && !ev->repeat)   enter(d);
    else if (ev->scan_code == AT_SC_BACKSPACE)            { if (len) d->pat_text[len - 1] = '\0'; d->pat_note[0] = '\0'; }
    else if (allowed(d, ev->ch) && len < max)             { d->pat_text[len] = ev->ch; d->pat_text[len + 1] = '\0'; d->pat_note[0] = '\0'; }
    return true;                                            // everything else is swallowed while typing
}

// ─── Status line ─────────────────────────────────────────────────────────────

void drumpad_pattern_edit_status(const AT_Drumpad* d, char* out, size_t cap) {
    const AT_DrumPattern* p = &d->pat_draft;
    char tail[56] = "";
    if (d->pat_note[0]) snprintf(tail, sizeof tail, "  (%s)", d->pat_note);

    if (d->pat_stage == STAGE_SIZE) {
        snprintf(out, cap, "* pattern size rows x steps (now %dx%d): %s_%s", p->rows, p->steps, d->pat_text, tail);
    } else if (d->pat_stage == STAGE_TIME) {
        snprintf(out, cap, "* step time ms or s (now %.0f, 0 = stop, 8x4 = size): %s_%s", p->step_ms, d->pat_text, tail);
    } else {
        int  r = d->pat_stage - STAGE_ROW;
        char key[16], now[AT_PATTERN_MAX_STEPS + 1];
        key_name(drumpad_pad_key(r), key, sizeof key);
        row_text(p, r, now);
        snprintf(out, cap, "* row %d/%d %s %.10s (now %s): %s_%s", r + 1, p->rows, key,
                 d->pads[r].loaded ? d->pads[r].label : "-", now, d->pat_text, tail);
    }
}
