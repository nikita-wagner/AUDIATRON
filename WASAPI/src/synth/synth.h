// ═══════════════════════════════════════════════════════════════════════════
//  synth/synth.h — polyphonic synth: N voices of (oscillator × ADSR)
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

#define AT_SYNTH_VOICES 32          // polyphony

typedef struct {
    bool          active;
    int           note;             // MIDI note number (60 = C4)
    float         velocity;         // 0..1
    AT_Oscillator osc;
    AT_ADSR       env;
    uint32_t      age;              // allocation order, for stealing
} AT_SynthVoice;

typedef struct {
    AT_SynthVoice voices[AT_SYNTH_VOICES];
    AT_Waveform   wave;
    AT_ADSRParams adsr;
    float         gain;             // output gain (headroom for many voices)
    float         detune_cents;     // pitch offset of every note (100 = one semitone up)
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
    AT_SYNTH_PARAM_DETUNE           // cents; applies to notes started afterwards
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
