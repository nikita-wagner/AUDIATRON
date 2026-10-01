#include "backend/select/backend_select.h"

#include <stdio.h>
#include <string.h>

#include "backend/winmm/winmm_backend.h"
#ifndef AT_NO_WASAPI
#include "backend/wasapi/wasapi_backend.h"
#endif

// One instance of each. Preference order for AT_BACKEND_AUTO: newest first.
static AT_WinmmBackend  s_winmm;
#ifndef AT_NO_WASAPI
static AT_WasapiBackend s_wasapi;
#endif

bool backend_choice_parse(const char* name, AT_BackendChoice* out) {
    if      (!strcmp(name, "auto"))   *out = AT_BACKEND_AUTO;
    else if (!strcmp(name, "wasapi")) *out = AT_BACKEND_WASAPI;
    else if (!strcmp(name, "winmm"))  *out = AT_BACKEND_WINMM;
    else return false;
    return true;
}

// The backends to try for a choice, best first. Returns how many.
static int candidates(AT_BackendChoice choice, AT_Backend list[2]) {
    int n = 0;
#ifndef AT_NO_WASAPI
    if (choice == AT_BACKEND_AUTO || choice == AT_BACKEND_WASAPI) list[n++] = wasapi_backend(&s_wasapi);
#endif
    if (choice == AT_BACKEND_AUTO || choice == AT_BACKEND_WINMM)  list[n++] = winmm_backend(&s_winmm);
    return n;
}

bool backend_select(AT_BackendChoice choice, AT_Backend* out) {
    AT_Backend list[2];
    int        count = candidates(choice, list);

    for (int i = 0; i < count; i++) {
        if (!backend_available(&list[i])) continue;
        *out = list[i];
        return true;
    }
    return false;
}

bool backend_select_open(AT_BackendChoice choice, const AT_BackendConfig* cfg, AT_Backend* out) {
    AT_Backend list[2];
    int        count = candidates(choice, list);

    if (count == 0) {                   // asked for WASAPI in a build without it
        *out = winmm_backend(&s_winmm);
        snprintf(out->info->error, sizeof out->info->error, "this build contains no WASAPI backend");
        return false;
    }

    AT_BackendConfig config = *cfg;

    for (int i = 0; i < count; i++) {
        *out = list[i];
        if (!backend_available(out)) {
            snprintf(out->info->error, sizeof out->info->error,
                     "the %s backend is not available on this Windows", out->name);
            continue;
        }
        if (backend_open(out, &config)) return true;

        // Device indices belong to the backend that listed them. The next
        // backend numbers its devices differently, so it gets the default one.
        config.device_index = -1;
    }
    return false;
}
