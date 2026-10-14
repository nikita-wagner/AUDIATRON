// ═══════════════════════════════════════════════════════════════════════════
//  app/app_args.c — command line → AT_AppOptions
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>

// ─── "file(0.2,0.9)": part of a sound file ───────────────────────────────────

// One number, optionally followed by "s" (seconds). Advances *text.
static bool parse_cut_value(const char** text, float* value, bool* seconds) {
    char*  end;
    double v = strtod(*text, &end);
    if (end == *text || v < 0.0) return false;

    if ((end[0] == 'm' || end[0] == 'M') && (end[1] == 's' || end[1] == 'S')) {   // "250ms"
        *seconds = true;
        v       /= 1000.0;
        end     += 2;
    } else {                                                                       // "0.25s" or a fraction
        *seconds = *end == 's' || *end == 'S';
        if (*seconds) end++;
    }
    *value = (float)v;
    *text  = end;
    return true;
}

// If `arg` ends in "(a)" or "(a,b)" with numbers inside, cuts that off the
// path (argv may be changed) and fills `cut`. Anything else stays part of the
// file name, e.g. "take (2)".
static void split_cut(char* arg, AT_AppCut* cut) {
    memset(cut, 0, sizeof *cut);

    size_t length = strlen(arg);
    char*  open   = strrchr(arg, '(');
    if (length < 3 || arg[length - 1] != ')' || !open || open == arg) return;

    AT_AppCut   c    = { .set = true };
    const char* text = open + 1;
    if (!parse_cut_value(&text, &c.to, &c.to_seconds)) return;

    if (*text == ',') {                     // "(from,to)"
        text++;
        c.from         = c.to;
        c.from_seconds = c.to_seconds;
        if (!parse_cut_value(&text, &c.to, &c.to_seconds)) return;
    }                                       // else "(to)": from the start

    if (*text == ',') {                     // "(from,to,wait,play for)": times, plain numbers are ms
        bool seconds;
        text++;
        if (!parse_cut_value(&text, &c.wait_s, &seconds)) return;
        if (!seconds) c.wait_s /= 1000.0f;
        if (*text != ',') return;
        text++;
        if (!parse_cut_value(&text, &c.play_s, &seconds)) return;
        if (!seconds) c.play_s /= 1000.0f;
        c.timed = true;
    }
    if (*text != ')' || text[1] != '\0') return;

    *open = '\0';
    *cut  = c;
}

bool app_cut_resolve(const AT_AppCut* cut, double seconds, float* from, float* to) {
    *from = 0.0f;
    *to   = 1.0f;
    if (!cut->set) return true;
    if (seconds <= 0.0) return false;

    double f = cut->from_seconds ? cut->from / seconds : cut->from;
    double t = cut->to_seconds   ? cut->to   / seconds : cut->to;
    if (t > 1.0) t = 1.0;                   // "to" past the end = to the end
    if (f >= t) return false;

    *from = (float)f;
    *to   = (float)t;
    return true;
}

static void print_usage(void) {
    printf("Usage: audiatron [options]\n"
           "  (no options)             live: default device, shared mode, keyboard instruments\n"
           "  --backend NAME           auto (default), wasapi or winmm\n"
           "  --exclusive              WASAPI exclusive mode (lowest latency)\n"
           "  --device N               use device N from --list\n"
           "  --rate HZ                request a sample rate\n"
           "  --period-us US           request a device period\n"
           "  --queue-ms MS            WinMM: audio kept queued = latency (default 120; raise it if it crackles)\n"
           "  --fx NAME                start with an effect switched on (repeatable)\n"
           "  --track N                track the keyboard (or the offline demo) plays; default: the first --play/--sample/--pad track, else 1\n"
           "  --play FILE              add a player track for a WAV / MP3 file (repeatable, 4 at most)\n"
           "  --sample FILE            add a track that plays a WAV / MP3 file pitched across the piano keys\n"
           "  --pad FILE               put a WAV / MP3 file on the next drumpad pad (repeatable, 16 at most;\n"
           "                           all pads share one drumpad track)\n"
           "  --pads FOLDER            put every WAV / MP3 in FOLDER on the pads, in name order\n"
           "                           (without any sound option the \"sample\" folder is loaded this way)\n"
           "  FILE(TO) / FILE(FROM,TO) play only part of a --play / --sample / --pad file, as fractions of\n"
           "                           its length, or with ms / s: \"kick.mp3(250ms,1000ms)\" (750 ms, frame-exact),\n"
           "                           \"kick.mp3(0.5)\" = the first half, \"song.mp3(30s,45s)\". Quote it in PowerShell.\n"
           "                           On a --pad two more: wait and play for, \"kick.wav(250ms,1000ms,2000ms,1000ms)\"\n"
           "                           = 2 s after a press, 250..1000 ms of the file, sounding for 1 s.\n"
           "  --machine FILE|NAME      the machine track: a WAV / MP3, a saved engine (.atengine: its sound and setup),\n"
           "                           or petrol, jet, propeller, rocket (default petrol)\n"
           "  --replay FILE            play an input recording (F5) back from the start, on copies of the tracks (F8 again)\n"
           "  --record-dir DIR         where F5 / F6 / F7 save recordings (default: recordings)\n"
           "  --pitch SEMITONES        start pitch of the players, e.g. -3 or 5\n"
           "  --speed FACTOR           start speed of the players, e.g. 0.8 or 1.25\n"
           "  --keep-tempo             players change pitch without changing speed (granular shifter)\n"
           "  --list                   list output devices and exit\n"
           "  --info                   open the device, print the negotiated format, exit\n"
           "  --offline FILE [SECONDS] render a demo to a WAV file instead of a device\n");
}

bool app_parse_args(AT_AppOptions* o, int argc, char** argv) {
    memset(o, 0, sizeof *o);
    o->mode            = AT_APP_MODE_LIVE;
    o->backend         = AT_BACKEND_AUTO;
    o->device_index    = -1;
    o->offline_seconds = 5.0;
    o->track           = 0;                 // automatic: the first sound file's track, else track 1

    for (int i = 1; i < argc; i++) {
        const char* a        = argv[i];
        bool        has_next = i + 1 < argc;

        if      (!strcmp(a, "--exclusive"))               o->exclusive = true;
        else if (!strcmp(a, "--backend")   && has_next) {
            if (!backend_choice_parse(argv[++i], &o->backend)) {
                fprintf(stderr, "unknown backend \"%s\" (use auto, wasapi or winmm)\n", argv[i]);
                return false;
            }
        }
        else if (!strcmp(a, "--list"))                    o->mode = AT_APP_MODE_LIST;
        else if (!strcmp(a, "--info"))                    o->mode = AT_APP_MODE_INFO;
        else if (!strcmp(a, "--device")    && has_next)   o->device_index = atoi(argv[++i]);
        else if (!strcmp(a, "--rate")      && has_next)   o->sample_rate  = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--period-us") && has_next)   o->period_us    = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--queue-ms")  && has_next)   o->queue_ms     = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--track")     && has_next)   o->track        = atoi(argv[++i]);
        else if (!strcmp(a, "--pitch")     && has_next)   o->pitch        = (float)atof(argv[++i]);
        else if (!strcmp(a, "--speed")     && has_next)   o->speed        = (float)atof(argv[++i]);
        else if (!strcmp(a, "--keep-tempo"))              o->keep_tempo   = true;
        else if ((!strcmp(a, "--play") || !strcmp(a, "--sample")) && has_next) {
            if (o->sound_count == AT_APP_MAX_SOUNDS) { fprintf(stderr, "too many --play / --sample files (%d at most)\n", AT_APP_MAX_SOUNDS); return false; }
            o->sound_is_sampler[o->sound_count] = !strcmp(a, "--sample");
            split_cut(argv[++i], &o->sound_cut[o->sound_count]);
            o->sound_path[o->sound_count++]     = argv[i];
        }
        else if (!strcmp(a, "--pad")       && has_next) {
            if (o->pad_count == AT_APP_MAX_PADS) { fprintf(stderr, "too many --pad files (%d at most)\n", AT_APP_MAX_PADS); return false; }
            split_cut(argv[++i], &o->pad_cut[o->pad_count]);
            o->pad_path[o->pad_count++] = argv[i];
        }
        else if (!strcmp(a, "--pads")      && has_next)   o->pad_folder   = argv[++i];
        else if (!strcmp(a, "--replay")    && has_next)   o->replay_path  = argv[++i];
        else if (!strcmp(a, "--machine")   && has_next)   o->machine_path = argv[++i];
        else if (!strcmp(a, "--record-dir") && has_next)  o->record_dir   = argv[++i];
        else if (!strcmp(a, "--fx")        && has_next) {
            if (o->fx_count == AT_APP_MAX_FX_FLAGS) { fprintf(stderr, "too many --fx options\n"); return false; }
            o->fx[o->fx_count++] = argv[++i];
        }
        else if (!strcmp(a, "--offline")   && has_next) {
            o->mode         = AT_APP_MODE_OFFLINE;
            o->offline_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') o->offline_seconds = atof(argv[++i]);
        }
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { print_usage(); return false; }
        else {
            fprintf(stderr, "unknown or incomplete argument: %s\n\n", a);
            print_usage();
            return false;
        }
    }
    return true;
}
