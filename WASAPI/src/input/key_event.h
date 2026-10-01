// ═══════════════════════════════════════════════════════════════════════════
//  input/key_event.h — one key press/release + names for the physical keys
//
//  Keys are identified by SCAN CODE (physical position), not by the letter
//  printed on them: a piano layout then works identically on QWERTZ and
//  QWERTY. The names below follow the US layout; on a German keyboard
//  AT_SC_Y is the key labelled Z, AT_SC_Z the one labelled Y, AT_SC_SEMICOLON
//  is Ö.
//
//  Any input module (console today; a window or MIDI-to-key mapper later)
//  produces AT_KeyEvents, any instrument consumes them.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t scan_code;     // AT_SC_*
    bool     down;          // true = pressed, false = released
    bool     repeat;        // true = auto-repeat while the key is held (down only)
} AT_KeyEvent;

enum {
    AT_SC_ESC = 0x01,

    // number row
    AT_SC_1 = 0x02, AT_SC_2, AT_SC_3, AT_SC_4, AT_SC_5, AT_SC_6, AT_SC_7, AT_SC_8, AT_SC_9, AT_SC_0,

    AT_SC_TAB = 0x0F,

    // top letter row
    AT_SC_Q = 0x10, AT_SC_W, AT_SC_E, AT_SC_R, AT_SC_T, AT_SC_Y, AT_SC_U, AT_SC_I, AT_SC_O, AT_SC_P,

    AT_SC_ENTER = 0x1C,

    // home row
    AT_SC_A = 0x1E, AT_SC_S, AT_SC_D, AT_SC_F, AT_SC_G, AT_SC_H, AT_SC_J, AT_SC_K, AT_SC_L, AT_SC_SEMICOLON,

    // bottom row
    AT_SC_Z = 0x2C, AT_SC_X, AT_SC_C, AT_SC_V, AT_SC_B, AT_SC_N, AT_SC_M,
    AT_SC_COMMA, AT_SC_PERIOD, AT_SC_SLASH,

    AT_SC_SPACE = 0x39,

    // function keys
    AT_SC_F1 = 0x3B, AT_SC_F2, AT_SC_F3, AT_SC_F4, AT_SC_F5, AT_SC_F6, AT_SC_F7, AT_SC_F8, AT_SC_F9, AT_SC_F10
};

#define AT_SC_COUNT 256
