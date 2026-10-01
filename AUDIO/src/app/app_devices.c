// ═══════════════════════════════════════════════════════════════════════════
//  app/app_devices.c — --list, --info, choosing the backend, opening the device
//
//  Which backend runs is decided here, once: WASAPI where Windows has it,
//  WinMM otherwise, or whatever --backend asks for. Everything after this
//  point talks to app->backend and does not care which one it is.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

// ─── --list ──────────────────────────────────────────────────────────────────

static void on_list_device(uint32_t index, const char* name, bool is_default, void* user) {
    (void)user;
    printf("  [%u]%s %s\n", index, is_default ? " *" : "  ", name);
}

int app_run_list(const AT_App* app) {
    AT_Backend backend;
    if (!backend_select(app->options.backend, &backend)) {
        fprintf(stderr, "that backend is not available on this Windows (or not part of this build)\n");
        return 1;
    }

    printf("Output devices of the %s backend (* = default):\n", backend.name);
    if (!backend_enumerate(&backend, on_list_device, NULL)) {
        fprintf(stderr, "enumeration failed\n");
        return 1;
    }
    return 0;
}

// ─── Opening ─────────────────────────────────────────────────────────────────

static void print_stream_info(const AT_Backend* b) {
    const AT_BackendInfo* i = b->info;

    printf("Backend: %s%s\n", b->name, i->exclusive ? ", EXCLUSIVE mode" : "");
    printf("Device : %s\n", i->device_name);
    printf("Format : %u Hz, %u ch, %s (%u valid bits)\n",
           i->sample_rate, i->channels, sample_format_name(i->sample_type), i->valid_bits);
    printf("Buffer : period %.2f ms (%u frames), up to %.0f ms queued\n",
           i->period_ms, i->buffer_frames, i->latency_ms);
}

bool app_open_device(AT_App* app) {
    const AT_AppOptions* o = &app->options;

    AT_BackendConfig cfg = {
        .device_index = o->device_index,
        .sample_rate  = o->sample_rate,
        .period_us    = o->period_us,
        .queue_us     = o->queue_ms * 1000u,
        .exclusive    = o->exclusive
    };

    if (!backend_select_open(o->backend, &cfg, &app->backend)) {
        fprintf(stderr, "%s\n", app->backend.info->error);
        return false;
    }

    print_stream_info(&app->backend);
    return true;
}

// ─── --info ──────────────────────────────────────────────────────────────────

int app_run_info(AT_App* app) {
    if (!app_open_device(app)) return 1;
    backend_close(&app->backend);
    return 0;
}
