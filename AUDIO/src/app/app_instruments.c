// ═══════════════════════════════════════════════════════════════════════════
//  app/app_instruments.c — ◄ REGISTER NEW INSTRUMENTS HERE
//
//  Every instrument gets its own TRACK: its own mixer channel with EQ, effects
//  and fader (tracks/track_list.h). Tab selects the track the keyboard plays.
//
//  To add an instrument (after creating its folder under instruments/):
//    1. #include its header
//    2. add a static instance
//    3. init it and track_list_add() it in app_instruments_setup
//
//  To layer several sounds into ONE instrument instead, see
//  instruments/layered_keyboard/ (layers/layer_stack.h).
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include "instruments/keyboard_piano/keyboard_piano.h"
#include "instruments/layered_keyboard/layered_keyboard.h"

// The app is the composition root, so it owns the instances.
static AT_KeyboardPiano   s_piano;
static AT_LayeredKeyboard s_layers;

void app_instruments_setup(AT_App* app, float sample_rate) {
    track_list_init(&app->tracks, &app->engine);

    // track 1: the plain piano
    keyboard_piano_init(&s_piano, sample_rate);
    track_list_add(&app->tracks, keyboard_piano_instrument(&s_piano));

    // track 2: piano + strings + synth layered into one sound
    layered_keyboard_init(&s_layers, sample_rate);
    track_list_add(&app->tracks, layered_keyboard_instrument(&s_layers));

    // drumpad_init(&s_drumpad, sample_rate);
    // track_list_add(&app->tracks, drumpad_instrument(&s_drumpad));
}
