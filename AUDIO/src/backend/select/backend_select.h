// ═══════════════════════════════════════════════════════════════════════════
//  backend/select/backend_select.h — choosing the audio backend at run time
//
//  The one place that knows which backends exist. Everything else talks to
//  whatever AT_Backend it is handed.
//
//      AT_BACKEND_AUTO     WASAPI where Windows has it (Vista and later),
//                          otherwise WinMM (95 / 98 / Me / NT4 / 2000 / XP)
//      AT_BACKEND_WASAPI   WASAPI or nothing
//      AT_BACKEND_WINMM    WinMM, also on new Windows (to compare, or to test
//                          the old-Windows path on a new machine)
//
//  "Has it" is tested by trying, not by reading a version number: that stays
//  right on systems that report odd versions (compatibility mode, Wine).
//
//      AT_Backend       backend;
//      AT_BackendConfig cfg = { .device_index = -1 };
//      if (!backend_select_open(AT_BACKEND_AUTO, &cfg, &backend))
//          puts(backend.info->error);
//
//  Building without WASAPI (for old compilers and SDKs that lack its headers):
//  define AT_NO_WASAPI and leave backend/wasapi/ out of the build — see
//  "build.bat winmm". AT_BACKEND_AUTO then means WinMM.
//
//  This module owns one instance of each backend, so one stream can be open at
//  a time. For more, create your own AT_WasapiBackend / AT_WinmmBackend.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "backend/audio_backend.h"

typedef enum {
    AT_BACKEND_AUTO,
    AT_BACKEND_WASAPI,
    AT_BACKEND_WINMM
} AT_BackendChoice;

// "auto" / "wasapi" / "winmm" → choice. false = unknown name.
bool backend_choice_parse(const char* name, AT_BackendChoice* out);

// Hands out the backend for a choice without opening it (for listing devices).
// false = not compiled in, or not available on this Windows; *out is then
// untouched.
bool backend_select(AT_BackendChoice choice, AT_Backend* out);

// Selects AND opens. With AT_BACKEND_AUTO a backend that fails to open is
// followed by the next one, so a broken WASAPI setup still gets sound through
// WinMM. On failure *out is the backend tried last; its info->error says why.
bool backend_select_open(AT_BackendChoice choice, const AT_BackendConfig* cfg, AT_Backend* out);
