#include "instruments/fusion/fusion.h"

#include <stdio.h>
#include <string.h>

void fusion_init(AT_Fusion* f, AT_Instrument keyboard, AT_Drumpad* drumpad) {
    memset(f, 0, sizeof *f);
    f->keyboard = keyboard;
    f->drums    = drumpad_instrument(drumpad);
    f->drumpad  = drumpad;

    snprintf(f->help, sizeof f->help,
             "  main keyboard: the %s track, as there:\n%s"
             "  numpad: pads   1-9 = pads 1-9   0 = 10   . = 11   / = 12   * = 13   - = 14   + = 15   Enter = 16\n"
             "  hold a numpad pad, then: + / - pitch, arrows or , . volume / pan, L loop, P keep tempo,\n"
             "                           C V F1 effects, X sync, / cut, Backspace reset (on that pad)\n"
             "  no pad held: , . 9 0 Enter C V F1 act on the %s track\n",
             keyboard.name, keyboard.help ? keyboard.help : "", keyboard.name);
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void fusion_attach(void* self, AT_Engine* engine, uint32_t channel) {
    (void)self; (void)engine; (void)channel;    // no sound of its own: the two tracks play
}

static bool fusion_key_adapter(void* self, const AT_KeyEvent* ev) {
    return fusion_key((AT_Fusion*)self, ev);
}

static void fusion_release_all(void* self) {
    AT_Fusion* f = (AT_Fusion*)self;
    if (f->keyboard.release_all) f->keyboard.release_all(f->keyboard.self);
    if (f->drums.release_all)    f->drums.release_all(f->drums.self);
}

// e.g. "oct 4 piano+strings+synth | A kick.wav +0st sync v1.0 | 0 loops"
static void fusion_status(void* self, char* out, size_t cap) {
    AT_Fusion* f = (AT_Fusion*)self;
    char keys[64] = "", drums[96] = "";
    if (f->keyboard.status) f->keyboard.status(f->keyboard.self, keys, sizeof keys);
    if (f->drums.status)    f->drums.status(f->drums.self, drums, sizeof drums);
    snprintf(out, cap, "%s | %s", keys, drums);
}

AT_Instrument fusion_instrument(AT_Fusion* f) {
    return (AT_Instrument){
        .name        = "fusion",
        .help        = f->help,
        .self        = f,
        .attach      = fusion_attach,
        .key         = fusion_key_adapter,
        .release_all = fusion_release_all,
        .status      = fusion_status
    };
}
