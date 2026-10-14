// ═══════════════════════════════════════════════════════════════════════════
//  instruments/sample_player/sample_player.h — plays one WAV / MP3 file
//
//  A tape deck for a sound file: play, pause, stop, seek, loop, and two ways
//  of changing the pitch.
//
//      P   play / pause              S   stop (rewind)
//      L   loop on / off             Left / Right   seek -5 s / +5 s
//      Up / Down   pitch +1 / -1 semitone
//      Y / X       speed -5 % / +5 %          R   reset pitch and speed
//      T   tape mode <-> keep-tempo mode
//
//  Tape mode      pitch and speed are tied, like a record: raising the pitch
//                 also plays faster (the sampler's rate does it, sampler/).
//  Keep-tempo     speed and pitch are separate: the pitch shifter
//                 (effects/pitch_shift) corrects the pitch the speed change
//                 would cause, so "pitch +3" changes only the pitch and
//                 "speed 1.2" changes only the tempo. The price is the shifter's
//                 slight flutter; for large changes tape mode sounds cleaner.
//
//  The instrument owns its clip, its sampler and its pitch shifter. The shifter
//  sits on the track's SOURCE hook, so echo and the other track effects come
//  after it.
//
//  Files in this folder:
//    sample_player.c           load the file, attach to the engine, AT_Instrument
//    sample_player_keys.c      what each key does
//    sample_player_control.c   pitch / speed / mode: works out the sampler rate
//                              and shifter ratio and sends them to the engine
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "effects/pitch_shift/pitch_shift.h"
#include "instruments/instrument.h"
#include "io/audio_clip/audio_clip.h"
#include "sampler/sampler.h"

#define AT_PLAYER_PITCH_MIN   (-24.0f)      // semitones
#define AT_PLAYER_PITCH_MAX   ( 24.0f)
#define AT_PLAYER_SPEED_MIN   0.25f
#define AT_PLAYER_SPEED_MAX   4.0f

typedef struct {
    AT_Clip       clip;                     // the decoded file (owned unless this is a copy)
    bool          owns_clip;
    AT_Sampler    sampler;                  // the sound source (rendered by the engine)
    AT_PitchShift shifter;                  // source hook; idle (and bit-exact) at pitch 0 in keep-tempo mode

    AT_Engine*    engine;                   // NULL until attach: settings are then written directly
    uint32_t      channel;

    // control-thread view of the settings
    float         pitch;                    // semitones
    float         speed;                    // 1 = original
    bool          keep_tempo;
    bool          loop;

    char          label[40];                // file name without the folder, for the status line
    float         sample_rate;
} AT_SamplePlayer;

// Loads the file. false = failed, `error` says why. Free with sample_player_free().
bool sample_player_init(AT_SamplePlayer* p, const char* path, float sample_rate, char* error, size_t error_cap);
void sample_player_free(AT_SamplePlayer* p);

// A second player on the same file: shares the decoded audio (no second copy
// in memory), starts with fresh settings. `original` must outlive the copy.
void sample_player_init_copy(AT_SamplePlayer* p, const AT_SamplePlayer* original, float sample_rate);

// Describes the player to the app. `p` must outlive the returned struct's use.
AT_Instrument sample_player_instrument(AT_SamplePlayer* p);

// ─── sample_player_control.c ─────────────────────────────────────────────────
// Before attach these write the settings directly; afterwards they go through
// the engine's command queue. Values are clamped to the AT_PLAYER_* ranges.

void sample_player_set_pitch      (AT_SamplePlayer* p, float semitones);
void sample_player_set_speed      (AT_SamplePlayer* p, float speed);
void sample_player_set_keep_tempo (AT_SamplePlayer* p, bool keep_tempo);
void sample_player_set_loop       (AT_SamplePlayer* p, bool loop);
// Plays only from..to of the file (fractions of its length, e.g. 0.2 .. 0.9);
// play, stop, loop and seeking stay inside. false = out of order / outside 0..1.
bool sample_player_set_cut        (AT_SamplePlayer* p, float from, float to);
void sample_player_play           (AT_SamplePlayer* p);    // on a synced track the song joins the sync clock
void sample_player_play_now       (AT_SamplePlayer* p);    // from where it stands, never synced (a sequencer placed it)
void sample_player_pause          (AT_SamplePlayer* p);
void sample_player_stop           (AT_SamplePlayer* p);
void sample_player_seek_seconds   (AT_SamplePlayer* p, double seconds);     // absolute
double sample_player_position_seconds(const AT_SamplePlayer* p);            // approximate
bool   sample_player_is_playing   (const AT_SamplePlayer* p);               // approximate

// ─── used between the files of this folder ───────────────────────────────────

bool sample_player_key(AT_SamplePlayer* p, const AT_KeyEvent* ev);          // sample_player_keys.c
extern const char SAMPLE_PLAYER_HELP[];                                     // sample_player_keys.c
