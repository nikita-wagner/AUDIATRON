// ═══════════════════════════════════════════════════════════════════════════
//  instruments/fusion/fusion_keys.c — which key goes where (control thread)
//
//    1. the drumpad's "/" cut editor, while it is open: every key
//    2. a numpad pad key: to the drumpad, as that pad's own key
//    3. a pad is held: the drumpad's controls (+ - arrows L P C V F1 X / ...)
//    4. everything else: the keyboard instrument
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/fusion/fusion.h"

// numpad key → pad, in pad order (1..9, 0, . / * - + Enter)
typedef struct { uint16_t scan; bool extended; } NumpadKey;

static const NumpadKey k_numpad[AT_DRUMPAD_PADS] = {
    { AT_SC_NUM_1, false }, { AT_SC_NUM_2, false }, { AT_SC_NUM_3, false },
    { AT_SC_NUM_4, false }, { AT_SC_NUM_5, false }, { AT_SC_NUM_6, false },
    { AT_SC_NUM_7, false }, { AT_SC_NUM_8, false }, { AT_SC_NUM_9, false },
    { AT_SC_NUM_0, false }, { AT_SC_NUM_DOT, false },
    { AT_SC_SLASH, true  },                             // numpad '/' (the main-row key with this code is German '-')
    { AT_SC_NUM_STAR, false }, { AT_SC_NUM_MINUS, false }, { AT_SC_NUM_PLUS, false },
    { AT_SC_ENTER, true  },                             // numpad Enter
};

int fusion_numpad_pad(const AT_KeyEvent* ev) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++)
        if (k_numpad[i].scan == ev->scan_code && k_numpad[i].extended == ev->extended) return i;
    return -1;
}

bool fusion_pad_held(const AT_Fusion* f) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++)
        if (f->drumpad->pads[i].held) return true;
    return false;
}

// A letter that is a pad on the drumpad's own track (A..K, Q..I) is a note
// here: it must never reach the drumpad.
static bool is_drumpad_letter(uint16_t scan) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++)
        if (drumpad_pad_key(i) == scan) return true;
    return false;
}

static bool to_drums(AT_Fusion* f, const AT_KeyEvent* ev) {
    return f->drums.key && f->drums.key(f->drums.self, ev);
}

bool fusion_key(AT_Fusion* f, const AT_KeyEvent* ev) {
    int pad = fusion_numpad_pad(ev);

    // 1. the cut editor types digits: numpad digits stay digits there, but
    //    letting go of a numpad pad still lets go of the pad
    if (drumpad_editing(f->drumpad)) {                      // "/" or "*" editor
        if (pad >= 0 && !ev->down) {
            AT_KeyEvent up = *ev;
            up.scan_code = drumpad_pad_key(pad);
            up.extended  = false;
            return to_drums(f, &up);
        }
        return to_drums(f, ev);
    }

    // 2. a numpad pad: the drumpad sees its own key for that pad
    if (pad >= 0) {
        AT_KeyEvent hit = *ev;
        hit.scan_code = drumpad_pad_key(pad);
        hit.extended  = false;
        hit.ch        = 0;                              // a pad, not a typed character
        to_drums(f, &hit);
        return true;
    }

    // "*" from the main keyboard (Shift+'+'): the drumpad's pattern (numpad '*' is pad 13)
    if (ev->ch == '*') return to_drums(f, ev);

    // 3. a pad is held: its controls
    if (fusion_pad_held(f) && !is_drumpad_letter(ev->scan_code) && to_drums(f, ev)) return true;

    // 4. the keyboard instrument
    return f->keyboard.key && f->keyboard.key(f->keyboard.self, ev);
}
