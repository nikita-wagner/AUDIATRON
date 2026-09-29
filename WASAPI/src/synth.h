// ═══════════════════════════════════════════════════════════════════════════
//  synth.h — polyphonic oscillator synth with linear ADSR
//
//  One AT_Synth = one instrument with AT_MAX_VOICES voices. Renders MONO into
//  a float array; the engine places it into a stereo channel strip.
//
//  Voice allocation (fixes the WinMM "same note stacks and beats" problem at
//  the source instead of with phase lock):
//    1. note already sounding → that voice is retriggered, never duplicated
//    2. otherwise a free voice
//    3. otherwise steal: releasing voices first, then the oldest
//  Phase is never reset on retrigger/steal, so there is no waveform jump.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "at_common.h"

typedef enum {
    AT_WAVE_SINE,
    AT_WAVE_SAW,        // PolyBLEP band-limited
    AT_WAVE_SQUARE,     // PolyBLEP band-limited
    AT_WAVE_TRIANGLE,
    AT_WAVE_NOISE,
    AT_WAVE_COUNT
} AT_Waveform;

typedef enum {
    AT_ENV_IDLE,
    AT_ENV_ATTACK,
    AT_ENV_DECAY,
    AT_ENV_SUSTAIN,
    AT_ENV_RELEASE
} AT_EnvStage;

typedef struct {
    float attack_s;
    float decay_s;
    float sustain;      // level 0..1
    float release_s;
} AT_ADSRParams;

typedef struct {
    bool        active;
    int         note;           // MIDI note number (60 = C4)
    float       velocity;       // 0..1
    double      phase;          // normalized 0..1
    double      phase_inc;      // frequency / sample_rate
    AT_EnvStage stage;
    float       level;          // current envelope level
    float       release_step;
    uint32_t    age;            // allocation order, for stealing
    uint32_t    noise_state;
} AT_Voice;

typedef struct {
    AT_Voice      voices[AT_MAX_VOICES];
    AT_Waveform   wave;
    AT_ADSRParams adsr;
    float         gain;         // output gain (headroom for many voices)
    float         sample_rate;
    uint32_t      age_counter;
} AT_Synth;

const char* synth_waveform_name(AT_Waveform w);

// All of these are called from the audio thread (via the engine's command
// queue) — never call them directly from UI code while the stream runs.
void synth_init         (AT_Synth* s, float sample_rate);
void synth_note_on      (AT_Synth* s, int note, float velocity);
void synth_note_off     (AT_Synth* s, int note);
void synth_all_notes_off(AT_Synth* s);

// Overwrites out[0 .. frames-1].
void synth_render(AT_Synth* s, float* out, uint32_t frames);
