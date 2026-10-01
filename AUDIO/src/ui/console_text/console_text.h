// ═══════════════════════════════════════════════════════════════════════════
//  ui/console_text/console_text.h — text output helpers for the console
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// Call once at startup: makes the console print UTF-8 (device names with
// umlauts, box characters).
void console_text_init(void);

// Redraws ONE line in place: returns to the start of the line, prints `text`
// cut to the window width and blanks whatever the previous line left behind.
void console_text_status_line(const char* text);
