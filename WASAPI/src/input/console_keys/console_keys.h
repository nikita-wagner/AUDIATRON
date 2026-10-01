// ═══════════════════════════════════════════════════════════════════════════
//  input/console_keys/console_keys.h — raw key up/down events from the console
//
//  Switches the console to raw input (no echo, no line buffering) and turns
//  its key records into AT_KeyEvents. Unlike getch(), this reports key
//  RELEASES too — which is what makes holding a note possible.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "input/key_event.h"

typedef struct {
    HANDLE handle;
    DWORD  old_mode;
    bool   is_down[AT_SC_COUNT];    // to tell a first press from auto-repeat
} AT_ConsoleKeys;

// false = stdin is not a real console (piped / redirected).
bool console_keys_open (AT_ConsoleKeys* k);
void console_keys_close(AT_ConsoleKeys* k);         // restores the previous console mode

// Waits up to timeout_ms for input, then returns the key events that arrived
// (0 on timeout). Never returns more than `max` events.
uint32_t console_keys_poll(AT_ConsoleKeys* k, uint32_t timeout_ms, AT_KeyEvent* out, uint32_t max);
