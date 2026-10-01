#include "input/key_names/key_names.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

void key_name(uint16_t scan_code, char* out, size_t cap) {
    if (cap == 0) return;

    // GetKeyNameText takes the scan code in bits 16..23 of a keystroke message lParam.
    // The A variant + a conversion, because the W variant does nothing on Windows 95/98/Me.
    char    ansi[32];
    wchar_t wide[32];
    if (GetKeyNameTextA((LONG)scan_code << 16, ansi, sizeof ansi) <= 0 ||
        MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, 32) <= 0 ||
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, (int)cap, NULL, NULL) <= 0)
        snprintf(out, cap, "?");
}
