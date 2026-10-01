// ═══════════════════════════════════════════════════════════════════════════
//  instruments/keyboard_piano/keyboard_piano_patch.c — the piano's SOUND
//
//  Everything that decides how the instrument sounds lives here: synth
//  settings and the EQ of its mixer channel. Change the sound without
//  touching the key handling, and the other way round.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/keyboard_piano/keyboard_piano.h"

// Called from attach, BEFORE the synth is plugged into the engine — so the
// synth can still be written directly. Once it is plugged in, parameters
// must go through engine_set_param.
void keyboard_piano_load_patch(AT_KeyboardPiano* p) {
    p->synth.wave = AT_WAVE_SAW;
    p->synth.adsr = (AT_ADSRParams){ .attack_s = 0.005f, .decay_s = 0.25f, .sustain = 0.6f, .release_s = 0.4f };
    p->synth.gain = 0.2f;
    p->wave       = p->synth.wave;

    // channel EQ: cut rumble, tame the saw's top end
    engine_set_eq_band(p->engine, (int)p->channel, 0, AT_FILTER_HIGHPASS, 60.0f,   0.707f, 0.0f);
    engine_set_eq_band(p->engine, (int)p->channel, 1, AT_FILTER_LOWPASS,  5000.0f, 0.707f, 0.0f);
}
