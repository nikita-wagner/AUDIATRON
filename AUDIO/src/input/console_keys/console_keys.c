#include "input/console_keys/console_keys.h"

#include <string.h>

#define MAX_RECORDS 32

bool console_keys_open(AT_ConsoleKeys* k) {
    memset(k, 0, sizeof *k);
    k->handle = GetStdHandle(STD_INPUT_HANDLE);
    if (!GetConsoleMode(k->handle, &k->old_mode)) return false;

    SetConsoleMode(k->handle, ENABLE_EXTENDED_FLAGS);       // raw key events, no echo, no quick-edit
    return true;
}

void console_keys_close(AT_ConsoleKeys* k) {
    if (k->handle) SetConsoleMode(k->handle, k->old_mode);
    k->handle = NULL;
}

uint32_t console_keys_poll(AT_ConsoleKeys* k, uint32_t timeout_ms, AT_KeyEvent* out, uint32_t max) {
    if (WaitForSingleObject(k->handle, timeout_ms) != WAIT_OBJECT_0) return 0;

    INPUT_RECORD records[MAX_RECORDS];
    DWORD        count = 0;
    if (max > MAX_RECORDS) max = MAX_RECORDS;
    // The A variant: scan codes are the same, and it also works on Windows
    // 95/98/Me, where the W variant is an empty stub.
    if (!ReadConsoleInputA(k->handle, records, max, &count)) return 0;

    uint32_t n = 0;
    for (DWORD r = 0; r < count; r++) {
        if (records[r].EventType != KEY_EVENT) continue;        // mouse, resize, focus...
        const KEY_EVENT_RECORD* key = &records[r].Event.KeyEvent;

        uint16_t scan     = key->wVirtualScanCode & 0xFF;
        bool     down     = key->bKeyDown != 0;
        bool     extended = (key->dwControlKeyState & ENHANCED_KEY) != 0;    // Up arrow vs numpad 8
        uint32_t slot     = scan + (extended ? AT_SC_COUNT : 0);

        out[n].scan_code = scan;
        out[n].ch        = key->uChar.AsciiChar;           // layout + Shift applied by Windows
        out[n].down      = down;
        out[n].extended  = extended;
        out[n].repeat    = down && k->is_down[slot];
        k->is_down[slot] = down;
        n++;
    }
    return n;
}
