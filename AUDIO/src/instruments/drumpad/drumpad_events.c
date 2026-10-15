// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_events.c — what happens to a pad, as data
//
//  The drumpad's vocabulary for recording and replaying. One event = one call
//  of a drumpad_* control function, so applying a list of events does exactly
//  what the player did. As text (one line, without the time):
//
//      pad 1 A hit 1            pad 1 A loop start      pad 1 A loop stop
//      pad 1 A loop on          pad 1 A pitch +3        pad 1 A tempo on
//      pad 1 A volume 0.8       pad 1 A pan -0.2        pad 1 A crush on
//      pad 1 A tremolo off      pad 1 A echo on         pad 1 A sync off
//      pad 1 A cut 0.2 0.9      pad 1 A cut 250ms 1000ms        pad 1 A reset
//
//  "pad <number> <key> <what> [values]". The key label is only there to read;
//  parsing goes by the number and also accepts lines without the label.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input/key_names/key_names.h"

// ─── Reporting ───────────────────────────────────────────────────────────────

void drumpad_set_observer(AT_Drumpad* d, AT_PadObserver fn, void* user) {
    d->observer      = fn;
    d->observer_user = user;
}

void drumpad_emit(AT_Drumpad* d, AT_PadEventType type, int pad, int fx, float a, float b) {
    if (!d->observer || d->quiet > 0) return;
    AT_PadEvent ev = { .type = type, .pad = pad, .fx = fx, .a = a, .b = b };
    d->observer(d->observer_user, &ev);
}

static void report(AT_PadObserver fn, void* user, AT_PadEventType type, int pad, int fx, float a, float b) {
    AT_PadEvent ev = { .type = type, .pad = pad, .fx = fx, .a = a, .b = b };
    fn(user, &ev);
}

void drumpad_snapshot(AT_Drumpad* d, AT_PadObserver fn, void* user) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        const AT_DrumPad* p = &d->pads[i];
        if (!p->loaded) continue;

        report(fn, user, AT_PAD_EV_PITCH,      i, 0, p->pitch, 0.0f);
        report(fn, user, AT_PAD_EV_KEEP_TEMPO, i, 0, p->keep_tempo ? 1.0f : 0.0f, 0.0f);
        report(fn, user, AT_PAD_EV_VOLUME,     i, 0, p->volume, 0.0f);
        report(fn, user, AT_PAD_EV_PAN,        i, 0, p->pan, 0.0f);
        report(fn, user, AT_PAD_EV_SYNC,       i, 0, p->sync ? 1.0f : 0.0f, 0.0f);
        report(fn, user, AT_PAD_EV_ECHO_MS, i, 0, (float)p->echo_ms, 0.0f);
        report(fn, user, AT_PAD_EV_TIMING,  i, 0, p->wait_ms, p->play_ms);
        if (p->cut_in_ms) report(fn, user, AT_PAD_EV_CUT_MS, i, 0, p->cut_from_ms, p->cut_to_ms);
        else              report(fn, user, AT_PAD_EV_CUT,    i, 0, p->cut_from, p->cut_to);
        for (int f = 0; f < AT_PAD_FX_COUNT; f++)
            report(fn, user, AT_PAD_EV_FX, i, f, p->fx[f] ? 1.0f : 0.0f, 0.0f);
        report(fn, user, AT_PAD_EV_LOOP,       i, 0, p->loop ? 1.0f : 0.0f, 0.0f);
    }
    // Loops already running carry on in the recording from its start.
    for (int i = 0; i < AT_DRUMPAD_PADS; i++)
        if (drumpad_is_looping(d, i)) report(fn, user, AT_PAD_EV_LOOP_START, i, 0, 0.0f, 0.0f);
}

// ─── Applying ────────────────────────────────────────────────────────────────

void drumpad_event_apply(AT_Drumpad* d, const AT_PadEvent* ev) {
    int  pad = ev->pad;
    bool on  = ev->a != 0.0f;

    switch (ev->type) {
    case AT_PAD_EV_HIT:        drumpad_hit(d, pad, ev->a);                         break;
    case AT_PAD_EV_LOOP_START: drumpad_loop_start(d, pad);                         break;
    case AT_PAD_EV_LOOP_STOP:  drumpad_loop_stop(d, pad);                          break;
    case AT_PAD_EV_LOOP:       drumpad_set_loop(d, pad, on);                       break;
    case AT_PAD_EV_PITCH:      drumpad_set_pitch(d, pad, ev->a);                   break;
    case AT_PAD_EV_KEEP_TEMPO: drumpad_set_keep_tempo(d, pad, on);                 break;
    case AT_PAD_EV_VOLUME:     drumpad_set_volume(d, pad, ev->a);                  break;
    case AT_PAD_EV_PAN:        drumpad_set_pan(d, pad, ev->a);                     break;
    case AT_PAD_EV_FX:         drumpad_set_fx(d, pad, (AT_PadFx)ev->fx, on);       break;
    case AT_PAD_EV_SYNC:       drumpad_set_sync(d, pad, on);                       break;
    case AT_PAD_EV_CUT:        drumpad_set_cut(d, pad, ev->a, ev->b);              break;
    case AT_PAD_EV_CUT_MS:     drumpad_set_cut_ms(d, pad, ev->a, ev->b);           break;
    case AT_PAD_EV_ECHO_MS:    drumpad_set_echo_ms(d, pad, (int)ev->a);            break;
    case AT_PAD_EV_TIMING:     drumpad_set_timing(d, pad, ev->a, ev->b);           break;
    case AT_PAD_EV_RESET:      drumpad_reset(d, pad);                              break;
    }
}

// ─── Text ────────────────────────────────────────────────────────────────────

static const char* on_off(float v) { return v != 0.0f ? "on" : "off"; }

void drumpad_event_format(const AT_PadEvent* ev, char* out, size_t cap) {
    char key[16];
    key_name(drumpad_pad_key(ev->pad), key, sizeof key);
    if (!drumpad_pad_key(ev->pad) || !key[0]) snprintf(key, sizeof key, "-");     // a pad without a key
    for (char* c = key; *c; c++) if (*c == ' ') *c = '_';          // one word

    char what[64];
    switch (ev->type) {
    case AT_PAD_EV_HIT:        snprintf(what, sizeof what, "hit %g", ev->a);                        break;
    case AT_PAD_EV_LOOP_START: snprintf(what, sizeof what, "loop start");                           break;
    case AT_PAD_EV_LOOP_STOP:  snprintf(what, sizeof what, "loop stop");                            break;
    case AT_PAD_EV_LOOP:       snprintf(what, sizeof what, "loop %s", on_off(ev->a));               break;
    case AT_PAD_EV_PITCH:      snprintf(what, sizeof what, "pitch %+g", ev->a);                     break;
    case AT_PAD_EV_KEEP_TEMPO: snprintf(what, sizeof what, "tempo %s", on_off(ev->a));              break;
    case AT_PAD_EV_VOLUME:     snprintf(what, sizeof what, "volume %g", ev->a);                     break;
    case AT_PAD_EV_PAN:        snprintf(what, sizeof what, "pan %g", ev->a);                        break;
    case AT_PAD_EV_FX:         snprintf(what, sizeof what, "%s %s", AT_PAD_FX_NAMES[ev->fx], on_off(ev->a)); break;
    case AT_PAD_EV_SYNC:       snprintf(what, sizeof what, "sync %s", on_off(ev->a));               break;
    case AT_PAD_EV_CUT:        snprintf(what, sizeof what, "cut %g %g", ev->a, ev->b);              break;
    case AT_PAD_EV_CUT_MS:     snprintf(what, sizeof what, "cut %gms %gms", ev->a, ev->b);          break;
    case AT_PAD_EV_ECHO_MS:    snprintf(what, sizeof what, "echo_ms %d", (int)ev->a);               break;
    case AT_PAD_EV_TIMING:     snprintf(what, sizeof what, "timing %gms %gms", ev->a, ev->b);       break;
    case AT_PAD_EV_RESET:      snprintf(what, sizeof what, "reset");                                break;
    default:                   snprintf(what, sizeof what, "?");                                    break;
    }
    snprintf(out, cap, "pad %d %s %s", ev->pad + 1, key, what);
}

// The words of a line, at most `max`, each at most 31 characters.
static int split_words(const char* text, char words[][32], int max) {
    int n = 0;
    while (*text && n < max) {
        while (*text == ' ' || *text == '\t') text++;
        if (!*text) break;
        int len = 0;
        while (*text && *text != ' ' && *text != '\t') {
            if (len < 31) words[n][len++] = *text;
            text++;
        }
        words[n++][len] = '\0';
    }
    return n;
}

static const char* const k_verbs[] = { "hit", "loop", "pitch", "tempo", "volume", "pan", "crush", "tremolo", "echo", "echo_ms", "timing", "sync", "cut", "reset" };

static bool is_verb(const char* word) {
    for (size_t i = 0; i < sizeof k_verbs / sizeof *k_verbs; i++)
        if (!strcmp(word, k_verbs[i])) return true;
    return false;
}

// "on" / "off" → 1 / 0; false = neither.
static bool parse_on_off(const char* word, float* value) {
    if (!strcmp(word, "on"))  { *value = 1.0f; return true; }
    if (!strcmp(word, "off")) { *value = 0.0f; return true; }
    return false;
}

static bool parse_number(const char* word, float* value) {
    char* end;
    double v = strtod(word, &end);
    if (end == word || *end) return false;
    *value = (float)v;
    return true;
}

// "2000ms" or "2000"
static bool parse_ms(const char* word, float* value) {
    char   number[32];
    size_t n = strlen(word);
    if (n > 2 && !strcmp(word + n - 2, "ms")) n -= 2;
    snprintf(number, sizeof number, "%.*s", (int)n, word);
    return parse_number(number, value);
}

bool drumpad_event_parse(const char* text, AT_PadEvent* ev) {
    char words[8][32];
    int  n = split_words(text, words, 8);
    if (n < 3 || strcmp(words[0], "pad")) return false;

    int pad = atoi(words[1]);
    if (pad < 1 || pad > AT_DRUMPAD_PADS) return false;

    int v = is_verb(words[2]) ? 2 : 3;                      // the key label is optional
    if (v >= n || !is_verb(words[v])) return false;
    const char* verb  = words[v];
    const char* arg1  = v + 1 < n ? words[v + 1] : "";
    const char* arg2  = v + 2 < n ? words[v + 2] : "";

    memset(ev, 0, sizeof *ev);
    ev->pad = pad - 1;

    if (!strcmp(verb, "hit")) {
        ev->type = AT_PAD_EV_HIT;
        if (!parse_number(arg1, &ev->a)) ev->a = 1.0f;      // "pad 1 hit" = full velocity
        return true;
    }
    if (!strcmp(verb, "loop")) {
        if (!strcmp(arg1, "start")) { ev->type = AT_PAD_EV_LOOP_START; return true; }
        if (!strcmp(arg1, "stop"))  { ev->type = AT_PAD_EV_LOOP_STOP;  return true; }
        ev->type = AT_PAD_EV_LOOP;
        return parse_on_off(arg1, &ev->a);
    }
    if (!strcmp(verb, "pitch"))  { ev->type = AT_PAD_EV_PITCH;      return parse_number(arg1, &ev->a); }
    if (!strcmp(verb, "tempo"))  { ev->type = AT_PAD_EV_KEEP_TEMPO; return parse_on_off(arg1, &ev->a); }
    if (!strcmp(verb, "volume")) { ev->type = AT_PAD_EV_VOLUME;     return parse_number(arg1, &ev->a); }
    if (!strcmp(verb, "pan"))    { ev->type = AT_PAD_EV_PAN;        return parse_number(arg1, &ev->a); }
    if (!strcmp(verb, "sync"))   { ev->type = AT_PAD_EV_SYNC;       return parse_on_off(arg1, &ev->a); }
    if (!strcmp(verb, "reset"))  { ev->type = AT_PAD_EV_RESET;      return true; }
    if (!strcmp(verb, "echo_ms")) { ev->type = AT_PAD_EV_ECHO_MS;   return parse_number(arg1, &ev->a); }
    if (!strcmp(verb, "timing")) {                          // "timing 2000ms 1000ms" (wait, play for)
        ev->type = AT_PAD_EV_TIMING;
        return parse_ms(arg1, &ev->a) && parse_ms(arg2, &ev->b);
    }
    if (!strcmp(verb, "cut")) {
        // "cut 250ms 1000ms" (milliseconds) or "cut 0.2 0.9" (fractions of the file)
        size_t n1 = strlen(arg1), n2 = strlen(arg2);
        if (n1 > 2 && n2 > 2 && !strcmp(arg1 + n1 - 2, "ms") && !strcmp(arg2 + n2 - 2, "ms")) {
            char a[32], b[32];
            snprintf(a, sizeof a, "%.*s", (int)(n1 - 2), arg1);
            snprintf(b, sizeof b, "%.*s", (int)(n2 - 2), arg2);
            ev->type = AT_PAD_EV_CUT_MS;
            return parse_number(a, &ev->a) && parse_number(b, &ev->b);
        }
        ev->type = AT_PAD_EV_CUT;
        return parse_number(arg1, &ev->a) && parse_number(arg2, &ev->b);
    }
    for (int f = 0; f < AT_PAD_FX_COUNT; f++) {
        if (strcmp(verb, AT_PAD_FX_NAMES[f])) continue;
        ev->type = AT_PAD_EV_FX;
        ev->fx   = f;
        return parse_on_off(arg1, &ev->a);
    }
    return false;
}
