// ═══════════════════════════════════════════════════════════════════════════
//  sampler/sampler.h — plays a decoded sound file, pitched, looped, polyphonic
//
//  A sound source like the synth (core/at_module.h), but its raw material is an
//  AT_Clip (a WAV or MP3 loaded into memory, io/audio_clip). It knows nothing
//  about files, keyboards or the engine.
//
//  Two ways to play, which can be mixed:
//
//    NOTES        note_on(note) starts a new voice that plays the clip at the
//                 pitch of that note relative to the root note (default C4 = 60
//                 plays the clip unchanged, 72 an octave higher and twice as
//                 fast). Up to AT_SAMPLER_VOICES notes sound at once. A note
//                 plays to the end of the clip; with gate on, note_off ends it.
//
//    TRANSPORT    play / pause / stop / seek, for songs and loops. One
//                 transport voice, separate from the note voices.
//
//  Pitch: `rate` multiplies the speed of EVERY voice. Playing faster raises the
//  pitch by the same ratio (like a tape or a record); rate = 2^(semitones/12).
//  Keeping the tempo while changing the pitch is a different job, done by
//  effects/pitch_shift.
//
//  Region: only a part of the clip can be played, given as fractions of its
//  length (from 0.2 to 0.9 = from 20 % to 90 %) or in milliseconds (from 250
//  to 1000 ms). Milliseconds are exact: 750 ms is 36000 frames at 48 kHz in
//  every clip, so regions of equal length loop in step with each other. Notes and the transport start
//  at `from`, stop or loop at `to`. A cut edge in the middle of the sound is
//  faded over ~5 ms (a loop dips at its seam), so it does not click. The whole
//  clip (0..1) plays exactly as without a region.
//
//  Timing (both in milliseconds of real time, so the pitch does not change them):
//    delay    a note / the transport starts sounding this long after it is
//             started ("play it in 2 s")
//    length   it sounds this long ("for 1 s"): shorter than the region = cut
//             off with a 5 ms fade, longer = silence after the region. With
//             loop on, it repeats every `length` (first after `delay`), also
//             when that is longer than the region. 0 = the region's own length.
//  Both 0 (the default): everything plays exactly as without them.
//
//  Quality: the clip is read with 4-point cubic (Hermite) interpolation, so
//  pitching up does not turn into harsh aliasing the way nearest-sample or
//  linear reading does. Every start, stop and pause is faded over ~5 ms: no
//  clicks.
//
//  Files in this folder:
//    sampler.c          init, parameters, notes, transport, rendering
//    sampler_source.c   adapter that lets the engine use a sampler as a source
//
//  Threading: once plugged into the engine everything except the `position` /
//  `playing` readouts is audio-thread only. From the control thread use
//      engine_note_on / engine_note_off                          for notes
//      engine_set_param(e, sampler_set_param, &s, id, value)     for the rest
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"
#include "core/at_module.h"
#include "io/audio_clip/audio_clip.h"

#define AT_SAMPLER_VOICES   8
#define AT_SAMPLER_ROOT     60              // the note that plays the clip at its original pitch

typedef struct {
    bool     active;
    int      note;                          // MIDI note, or a transport / orphan marker
    float    velocity;                      // 0..1
    float    note_ratio;                    // speed factor from the note (2^((note - root) / 12))
    double   position;                      // frames into the clip (fractional)
    double   elapsed;                       // output frames since it started (delay / length timing)
    float    env;                           // smoothed 0..1 fade
    bool     releasing;                     // fading out for good
    bool     paused;                        // fading out, but keeps its place
    uint32_t age;                           // start order, for stealing
} AT_SamplerVoice;

typedef struct {
    const AT_Clip*  clip;                   // not owned; must outlive the sampler's use
    AT_SamplerVoice voices[AT_SAMPLER_VOICES];

    float    rate;                          // speed (and pitch) multiplier of all voices, 1 = original
    float    gain;                          // output level
    bool     loop;                          // wrap at the end of the clip instead of stopping
    bool     gate;                          // note_off ends a note (otherwise it plays to the end)
    int      root_note;
    float    region_from;                   // part of the clip that plays, fractions of its length
    float    region_to;                     //   (0 and 1 = the whole clip)
    float    region_from_ms;                // the same in milliseconds; when >= 0 it is used
    float    region_to_ms;                  //   instead of the fraction (-1 = not set)
    float    delay_ms;                      // timing (see above); 0 = none
    float    length_ms;

    double   start_position;                // where the transport voice starts (set by stop / seek)
    uint32_t age_counter;
    float    sample_rate;                   // of the engine
    float    fade_coef;                     // one-pole smoothing, ~5 ms

    // Optional clock the transport joins (sample_player sets it to the engine's
    // sync clock). Asked on the audio thread when PLAY starts the transport
    // afresh: true + *elapsed = start in phase (sampler_play_elapsed), false =
    // start as usual. PLAY with value 1 never asks (a sequencer placed it).
    bool  (*sync)(void* user, double* elapsed);
    void*  sync_user;

    // Readouts for the control thread (updated once per block; approximate).
    volatile int32_t position_frames;       // transport position
    volatile int32_t playing;               // 1 while the transport voice is audible
} AT_Sampler;

typedef enum {
    AT_SAMPLER_PARAM_PLAY,                  // start or resume the transport voice (1 = without joining the sync clock)
    AT_SAMPLER_PARAM_PAUSE,                 // value 1 = pause, 0 = resume
    AT_SAMPLER_PARAM_STOP,                  // fade out and rewind (value ignored)
    AT_SAMPLER_PARAM_SEEK,                  // seconds from the start of the clip
    AT_SAMPLER_PARAM_RATE,                  // speed / pitch multiplier, clamped to 0.05 .. 8
    AT_SAMPLER_PARAM_LOOP,                  // 0 / 1
    AT_SAMPLER_PARAM_GAIN,                  // linear
    AT_SAMPLER_PARAM_GATE,                  // 0 / 1
    AT_SAMPLER_PARAM_ROOT,                  // MIDI note that plays the clip unchanged
    AT_SAMPLER_PARAM_REGION_FROM,           // 0..1 of the clip length; kept below REGION_TO
    AT_SAMPLER_PARAM_REGION_TO,             // 0..1 of the clip length; kept above REGION_FROM
    AT_SAMPLER_PARAM_REGION_FROM_MS,        // milliseconds from the start of the clip; < 0 = use REGION_FROM
    AT_SAMPLER_PARAM_REGION_TO_MS,          // milliseconds (past the end = the end); < 0 = use REGION_TO
    AT_SAMPLER_PARAM_DELAY_MS,              // ms of silence before a note / the transport sounds
    AT_SAMPLER_PARAM_LENGTH_MS              // ms it sounds (with loop: repeats every this long); 0 = the region's own
} AT_SamplerParam;

// The clip may be NULL (the sampler is silent) and may be set later with sampler_set_clip.
void sampler_init(AT_Sampler* s, float sample_rate, const AT_Clip* clip);

// Swaps the clip. Only before the sampler is plugged into the engine (or while
// the audio thread is stopped): voices pointing into the old clip are dropped.
void sampler_set_clip(AT_Sampler* s, const AT_Clip* clip);

// Plays only from..to of the clip (fractions of its length, 0 <= from < to <= 1).
// Before plugging in; afterwards send AT_SAMPLER_PARAM_REGION_FROM / _TO.
void sampler_set_region(AT_Sampler* s, float from, float to);
// The same in milliseconds (frame-exact). to_ms past the clip's end = its end.
void sampler_set_region_ms(AT_Sampler* s, float from_ms, float to_ms);

// Audio thread only (call it from an AT_ParamFn): (re)starts the transport at
// clip frame `frame`, frame-exact. A running transport voice fades out; a
// frame outside the region starts at the region's beginning. Used to drop a
// loop into a running clock at the right phase.
void sampler_play_at(AT_Sampler* s, double frame);

// Audio thread only: (re)starts the transport as if it had been started
// `elapsed` output frames ago, so a loop joins a running clock in phase (with
// or without delay / length timing).
void sampler_play_elapsed(AT_Sampler* s, double elapsed);

void sampler_note_on      (AT_Sampler* s, int note, float velocity);
void sampler_note_off     (AT_Sampler* s, int note);
void sampler_all_notes_off(AT_Sampler* s);              // fades every note and the transport voice

// Adds the sound of all voices into `out` (stereo, cleared by the caller).
void sampler_render(AT_Sampler* s, AT_Buffer* out);

// AT_ParamFn — self = AT_Sampler*, param = AT_SamplerParam.
void sampler_set_param(void* self, int param, float value);

// Plug a sampler into an engine channel:
//     engine_set_source(&engine, channel, &AT_SAMPLER_SOURCE, &my_sampler);
extern const AT_SourceType AT_SAMPLER_SOURCE;
