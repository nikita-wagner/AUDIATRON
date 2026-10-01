// ═══════════════════════════════════════════════════════════════════════════
//  app/app_devices.c — --list, --info and opening the device the user chose
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdio.h>

#include "ui/console_text/console_text.h"

// ─── --list ──────────────────────────────────────────────────────────────────

static void on_list_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)id; (void)user;
    printf("  [%u]%s ", index, is_default ? " *" : "  ");
    console_text_print_wide(name);
    printf("\n");
}

int app_run_list(void) {
    printf("Output devices (* = default):\n");
    HRESULT hr = backend_enumerate_devices(on_list_device, NULL);
    if (FAILED(hr)) fprintf(stderr, "enumeration failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
    return FAILED(hr);
}

// ─── Opening ─────────────────────────────────────────────────────────────────

typedef struct { int wanted; wchar_t id[512]; bool found; } DevicePick;

static void on_pick_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)name; (void)is_default;
    DevicePick* pick = (DevicePick*)user;
    if ((int)index == pick->wanted) {
        wcsncpy_s(pick->id, sizeof pick->id / sizeof *pick->id, id, _TRUNCATE);
        pick->found = true;
    }
}

static void print_stream_info(const AT_Backend* b) {
    printf("Device : ");
    console_text_print_wide(b->device_name);
    printf("\nMode   : %s\n", b->share_mode == AT_SHARE_EXCLUSIVE ? "EXCLUSIVE" : "shared");
    printf("Format : %u Hz, %u ch, %s (%u valid bits)\n",
           b->sample_rate, b->channels, sample_format_name(b->sample_type), b->valid_bits);
    printf("Buffer : %u frames (%.2f ms), period %.2f ms\n",
           b->buffer_frames, 1000.0 * b->buffer_frames / b->sample_rate, b->period_ms);
}

bool app_open_device(AT_App* app) {
    const AT_AppOptions* o = &app->options;

    AT_BackendConfig cfg = {
        .share_mode  = o->share_mode,
        .sample_rate = o->sample_rate,
        .period_us   = o->period_us
    };

    DevicePick pick = { .wanted = o->device_index };
    if (o->device_index >= 0) {
        backend_enumerate_devices(on_pick_device, &pick);
        if (!pick.found) {
            fprintf(stderr, "no device with index %d (try --list)\n", o->device_index);
            return false;
        }
        cfg.device_id = pick.id;
    }

    HRESULT hr = backend_open(&app->backend, &cfg);
    if (FAILED(hr)) {
        fprintf(stderr, "backend_open failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
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
