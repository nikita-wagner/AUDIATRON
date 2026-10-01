// ═══════════════════════════════════════════════════════════════════════════
//  app/app_args.c — command line → AT_AppOptions
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>
#include <stdlib.h>

static void print_usage(void) {
    printf("Usage: audiatron [options]\n"
           "  (no options)             live: default device, shared mode, keyboard instruments\n"
           "  --exclusive              exclusive mode (lowest latency)\n"
           "  --device N               use device N from --list\n"
           "  --rate HZ                request a sample rate\n"
           "  --period-us US           request a device period\n"
           "  --fx NAME                start with an effect switched on (repeatable)\n"
           "  --track N                track the keyboard (or the offline demo) plays, default 1\n"
           "  --list                   list output devices and exit\n"
           "  --info                   open the device, print the negotiated format, exit\n"
           "  --offline FILE [SECONDS] render a demo to a WAV file instead of a device\n");
}

bool app_parse_args(AT_AppOptions* o, int argc, char** argv) {
    memset(o, 0, sizeof *o);
    o->mode            = AT_APP_MODE_LIVE;
    o->share_mode      = AT_SHARE_SHARED;
    o->device_index    = -1;
    o->offline_seconds = 5.0;
    o->track           = 1;

    for (int i = 1; i < argc; i++) {
        const char* a        = argv[i];
        bool        has_next = i + 1 < argc;

        if      (!strcmp(a, "--exclusive"))               o->share_mode = AT_SHARE_EXCLUSIVE;
        else if (!strcmp(a, "--list"))                    o->mode = AT_APP_MODE_LIST;
        else if (!strcmp(a, "--info"))                    o->mode = AT_APP_MODE_INFO;
        else if (!strcmp(a, "--device")    && has_next)   o->device_index = atoi(argv[++i]);
        else if (!strcmp(a, "--rate")      && has_next)   o->sample_rate  = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--period-us") && has_next)   o->period_us    = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--track")     && has_next)   o->track        = atoi(argv[++i]);
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
