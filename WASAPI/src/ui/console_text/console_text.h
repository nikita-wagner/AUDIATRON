// ═══════════════════════════════════════════════════════════════════════════
//  ui/console_text/console_text.h — text output helpers for the console
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <wchar.h>

// Call once at startup: makes the console print UTF-8 (device names with
// umlauts, box characters).
void console_text_init(void);

// Prints a wide string (what Windows APIs return) to stdout.
void console_text_print_wide(const wchar_t* s);

// Redraws ONE line in place: returns to the start of the line, prints `text`
// cut to the window width and blanks whatever the previous line left behind.
void console_text_status_line(const char* text);
