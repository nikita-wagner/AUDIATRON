// ═══════════════════════════════════════════════════════════════════════════
//  synth/synth.h — polyphonic synth: N voices of (oscillator × ADSR)
//
//  Every voice can also be shaped like an acoustic instrument: a resonant
//  low-pass that opens at the start of a note and closes again (brass, the
//  bite of a bow), vibrato that fades in after a moment (violin, trumpet), a
//  scoop up onto the note (brass), breath / bow noise, and instead of an
//  oscillator a plucked string (AT_WAVE_PLUCK: guitar, pizzicato).
//
//  One AT_Synth = one sound generator with AT_SYNTH_VOICES voices. It is built
//  from dsp/oscillator and dsp/envelope and knows nothing about the engine,
//  the mixer or keyboards.
//
//  Files in this folder:
//    synth.c          init, parameters, note on/off, rendering
//    synth_voices.c   voice allocation (which voice plays a new note)
//    synth_source.c   adapter that lets the engine use a synth as a source
//
//  Threading: once a synth is plugged into the engine, every function here is
//  audio-thread only. From the control thread use
//      engine_note_on / engine_note_off                      for notes
//      engine_set_param(e, synth_set_param, &synth, id, v)   for parameters
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"
#include "core/at_module.h"
#include "dsp/oscillator/oscillator.h"
#include "dsp/envelope/adsr.h"

#define AT_SYNTH_VOICES    32       // polyphony
#define AT_SYNTH_PLUCK_MAX 4096     // a plucked string's delay line: one period, down to ~12 Hz at 48 kHz

typedef struct {
    bool          active;
    int           note;             // MIDI note number (60 = C4)
    float         velocity;         // 0..1
    AT_Oscillator osc;
    AT_ADSR       env;
    uint32_t      age;              // allocation order, for stealing

    uint64_t      frames;           // since its note-on (vibrato, scoop)
    double        hz;               // its pitch, without vibrato and scoop
    float         fenv;             // the filter's envelope: 1 at the note-on, then falling
    float         lp1, lp2;         // the filter's state
    uint32_t      rng;              // noise
    // a plucked string (AT_WAVE_PLUCK)
    float         pluck[AT_SYNTH_PLUCK_MAX];
    int           pluck_len, pluck_pos;
    float         pluck_prev, pluck_ap, pluck_ap_in, pluck_c, pluck_gain;
} AT_SynthVoice;

typedef struct {
    AT_SynthVoice voices[AT_SYNTH_VOICES];
    AT_Waveform   wave;
    AT_ADSRParams adsr;
    float         gain;             // output gain (headroom for many voices)
    float         detune_cents;     // pitch offset of every note (100 = one semitone up)
    float         filter_hz;        // low-pass cutoff at C4 (0 = no filter); higher notes are brighter
    float         filter_res;       // 0..1
    float         filter_env;       // octaves it opens at a note's start, closing over filter_decay
    float         filter_decay;     // seconds
    float         vibrato_hz;
    float         vibrato_cents;    // 0 = none
    float         vibrato_delay;    // seconds before it fades in
    float         bend_cents;       // a note starts this far off and glides onto its pitch (-50: a scoop up)
    float         bend_time;        // seconds (time constant of the glide)
    float         noise;            // breath / bow noise, 0..1, through the filter
    float         pluck_bright;     // AT_WAVE_PLUCK: the pick, 0 = soft and dark .. 1 = bright
    float         pluck_decay;      // AT_WAVE_PLUCK: seconds until the string has died away (-60 dB)
    float         sample_rate;
    uint32_t      age_counter;
} AT_Synth;

typedef enum {
    AT_SYNTH_PARAM_WAVEFORM,        // value = AT_Waveform
    AT_SYNTH_PARAM_ATTACK,          // seconds
    AT_SYNTH_PARAM_DECAY,           // seconds
    AT_SYNTH_PARAM_SUSTAIN,         // level 0..1
    AT_SYNTH_PARAM_RELEASE,         // seconds
    AT_SYNTH_PARAM_GAIN,            // linear
    AT_SYNTH_PARAM_DETUNE,          // cents; applies to notes started afterwards
    AT_SYNTH_PARAM_FILTER,          // Hz at C4, 0 = off
    AT_SYNTH_PARAM_RESONANCE,       // 0..1
    AT_SYNTH_PARAM_FILTER_ENV,      // octaves
    AT_SYNTH_PARAM_FILTER_DECAY,    // seconds
    AT_SYNTH_PARAM_VIBRATO_RATE,    // Hz
    AT_SYNTH_PARAM_VIBRATO_DEPTH,   // cents
    AT_SYNTH_PARAM_VIBRATO_DELAY,   // seconds
    AT_SYNTH_PARAM_BEND,            // cents
    AT_SYNTH_PARAM_BEND_TIME,       // seconds
    AT_SYNTH_PARAM_NOISE,           // 0..1
    AT_SYNTH_PARAM_PLUCK_BRIGHT,    // 0..1
    AT_SYNTH_PARAM_PLUCK_DECAY      // seconds
} AT_SynthParam;

void synth_init         (AT_Synth* s, float sample_rate);
void synth_note_on      (AT_Synth* s, int note, float velocity);
void synth_note_off     (AT_Synth* s, int note);
void synth_all_notes_off(AT_Synth* s);

// Overwrites out[0 .. frames-1] with the mono sum of all voices.
void synth_render_mono(AT_Synth* s, float* out, uint32_t frames);

// AT_ParamFn — self = AT_Synth*, param = AT_SynthParam.
void synth_set_param(void* self, int param, float value);

// Plug a synth into an engine channel:
//     engine_set_source(&engine, channel, &AT_SYNTH_SOURCE, &my_synth);
extern const AT_SourceType AT_SYNTH_SOURCE;
