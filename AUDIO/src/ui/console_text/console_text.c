#include "ui/console_text/console_text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>

#define FALLBACK_WIDTH 80

void console_text_init(void) {
    SetConsoleOutputCP(CP_UTF8);
}

static int window_width(void) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) return FALLBACK_WIDTH;
    return info.srWindow.Right - info.srWindow.Left + 1;
}

void console_text_status_line(const char* text) {
    // One column short of the width: writing the last column would wrap the
    // cursor to the next line and '\r' could no longer reach the line start.
    int width = window_width() - 1;
    if (width < 1) width = 1;

    printf("\r%-*.*s", width, width, text);
    fflush(stdout);
}
