// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_cut_edit.c — "/": type a pad's timing in milliseconds
//
//  Hold a pad, press "/": the status line asks for four values, in ms:
//
//      hold F, "/"      F 1/4 start ms (file 2350): _          the part of the file
//      2 5 0 Enter      F 2/4 end ms (250.., empty = file end): _
//      1 0 0 0 Enter    F 3/4 wait ms before it plays (now 0): _
//      2 0 0 0 Enter    F 4/4 play for ms (0 = part length, now 0): _
//      1 0 0 0 Enter    done: 2 s after a press, F plays 250..1000 ms of its
//                       file, for 1 s (with loop: again every 1 s)
//
//  Digits from the number row or the numpad; Backspace deletes one; an empty
//  Enter keeps that value (a pad that is not cut ends at the end of its file),
//  so "250 Enter 1000 Enter Enter Enter" only cuts and "Enter Enter 2000 Enter
//  1000 Enter" only times; 0 switches wait / play-for off; Esc cancels
//  everything. While the editor is open it takes every key, so a typed digit
//  never plays anything. "/" is recognised by the character it
//  types, so it is Shift+7 on a German keyboard, "/" on a US one, and the
//  numpad "/" on both.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void close_editor(AT_Drumpad* d) {
    d->edit_pad     = -1;
    d->edit_text[0] = '\0';
    d->edit_note[0] = '\0';
}

bool drumpad_cut_edit_start(AT_Drumpad* d) {
    for (int i = 0; i < AT_DRUMPAD_PADS; i++) {
        AT_DrumPad* p = &d->pads[i];
        if (!p->held || !p->loaded) continue;

        p->changed_while_held = true;                       // this press does not stop the pad's loop
        d->edit_pad     = i;
        d->edit_field   = 0;
        d->edit_text[0] = '\0';
        d->edit_note[0] = '\0';
        d->selected     = i;
        return true;
    }
    return false;                                           // "/" without a held pad: nothing to cut
}

// The pad's current cut start, in ms.
static float current_start_ms(const AT_Drumpad* d, int pad) {
    const AT_DrumPad* p = &d->pads[pad];
    return p->cut_in_ms ? p->cut_from_ms : (float)(p->cut_from * drumpad_length_ms(d, pad));
}

static void enter(AT_Drumpad* d) {
    int    pad    = d->edit_pad;
    double length = drumpad_length_ms(d, pad);
    bool   empty  = d->edit_text[0] == '\0';
    float  value  = (float)atof(d->edit_text);

    const AT_DrumPad* p = &d->pads[pad];

    switch (d->edit_field) {
    case 0:                                                 // start
        if (empty) value = current_start_ms(d, pad);
        if (value >= length) { snprintf(d->edit_note, sizeof d->edit_note, "start must be below %.0f", length); return; }
        d->edit_from_ms = value;
        break;
    case 1:                                                 // end
        if (empty) value = p->cut_in_ms ? p->cut_to_ms : (float)(p->cut_to * length);
        if (value <= d->edit_from_ms) { snprintf(d->edit_note, sizeof d->edit_note, "end must be after %.0f", d->edit_from_ms); return; }
        d->edit_to_ms = value;
        break;
    case 2:                                                 // wait
        d->edit_wait_ms = empty ? p->wait_ms : value;
        break;
    default:                                                // play for: all four are known
        drumpad_set_cut_ms(d, pad, d->edit_from_ms, d->edit_to_ms);
        drumpad_set_timing(d, pad, d->edit_wait_ms, empty ? p->play_ms : value);
        close_editor(d);
        return;
    }
    d->edit_field++;
    d->edit_text[0] = '\0';
    d->edit_note[0] = '\0';
}

bool drumpad_cut_edit_key(AT_Drumpad* d, int pad_of_key, const AT_KeyEvent* ev) {
    // Letting go of a pad still counts (so its "held" state stays true to the keyboard).
    if (!ev->down) {
        if (pad_of_key >= 0) d->pads[pad_of_key].held = false;
        return true;
    }

    size_t len = strlen(d->edit_text);
    if (ev->scan_code == AT_SC_ESC && !ev->repeat) {
        close_editor(d);
    } else if (ev->scan_code == AT_SC_ENTER && !ev->repeat) {
        enter(d);
    } else if (ev->scan_code == AT_SC_BACKSPACE) {
        if (len > 0) d->edit_text[len - 1] = '\0';
        d->edit_note[0] = '\0';
    } else if (ev->ch >= '0' && ev->ch <= '9' && len < sizeof d->edit_text - 1) {
        d->edit_text[len]     = ev->ch;
        d->edit_text[len + 1] = '\0';
        d->edit_note[0]       = '\0';
    }
    return true;                                            // everything else is swallowed while typing
}
