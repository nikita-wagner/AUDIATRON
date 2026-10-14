// ═══════════════════════════════════════════════════════════════════════════
//  instruments/audio_track/audio_track.h — sound files placed on a timeline
//
//  The track an arrangement puts audio on: any number of WAV / MP3 files,
//  each played as REGIONS, a part of a file (its cut) that sounds for a given
//  time, stretched or squeezed, reversed, with fades, a gain and a pitch.
//  Regions may overlap; up to AT_AUDIO_TRACK_VOICES sound at once.
//
//  It plays nothing by itself: a sequencer (AudioStudio's arrangement) says
//  when. audio_track_play() writes the region into a free slot and sends
//  "play slot k"; sent after engine_at() the region starts on that exact
//  frame. The audio thread copies the slot into a voice when the command
//  arrives, so the control thread never writes what the audio thread reads.
//
//  Stretch: `rate` = seconds of the file per second of the region (2: the
//  file plays twice as fast, the region is half as long). By default the
//  pitch goes with the speed, like tape (read directly, cubic). With
//  keep_pitch the pitch stays and only the speed changes: grains of the file
//  are read at the pitch and placed at the speed, two overlapping sine²
//  windows (like effects/pitch_shift, but reading the file itself, so there is
//  no added delay). `pitch` shifts in semitones on top, the length stays.
//  Every start and stop is faded over 3 ms, on top of the region's own fades.
//
//  Files in this folder:
//    audio_track.c          files, slots, control, the AT_Instrument adapter
//    audio_track_source.c   rendering the voices (audio thread)
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "instruments/instrument.h"
#include "io/audio_clip/audio_clip.h"

#define AT_AUDIO_TRACK_FILES   64           // different sound files on the track
#define AT_AUDIO_TRACK_SLOTS   64           // regions sent ahead (a ring; a slot is reused once its start has passed)
#define AT_AUDIO_TRACK_VOICES  16           // regions sounding at once

typedef struct {
    bool     loaded;
    AT_Clip  clip;                          // the decoded file (owned)
    char     path[260];
    char     label[64];                     // file name without the folder
} AT_AudioFile;

typedef struct {
    int      file;                          // index into the track's files
    double   offset_s;                      // where in the file the region begins (its cut), seconds
    double   length_s;                      // how long the region sounds, seconds of output
    double   start_s;                       // where in the region this play begins (0; more when a transport starts inside it)
    double   rate;                          // seconds of file per second of region: the stretch (0.05 .. 20)
    float    gain;                          // linear
    float    fade_in_s;
    float    fade_out_s;
    float    pitch;                         // semitones on top, the length stays (-24 .. 24)
    bool     keep_pitch;                    // a stretch keeps the pitch (else it goes with the speed, like tape)
    bool     reverse;                       // the part plays backwards
} AT_AudioRegion;

typedef struct {
    bool            active;
    int             slot;                   // where it came from (audio_track_stop names it)
    AT_AudioRegion  region;
    const AT_Clip*  clip;
    double          t;                      // output frames into the region
    double          end;                    // output frames: where the region ends
    float           env;                    // start / stop declick, 0..1
    bool            stopping;
    double          grain_anchor[2];        // keep_pitch / pitch: file frame where each grain began
    double          grain_t[2];             // output frames into each grain
} AT_AudioVoice;

typedef struct {
    AT_AudioFile    files[AT_AUDIO_TRACK_FILES];    // control thread loads; the clips stay put until free
    AT_AudioRegion  slots[AT_AUDIO_TRACK_SLOTS];    // control writes one, then sends "play" for it
    uint64_t        slot_due[AT_AUDIO_TRACK_SLOTS]; // control: the frame its play was sent for
    uint32_t        next_slot;
    AT_AudioVoice   voices[AT_AUDIO_TRACK_VOICES];  // audio thread
    float           sample_rate;
    AT_Engine*      engine;                 // NULL until attach
    uint32_t        channel;
    volatile int32_t shown_voices;          // regions sounding (approximate, for displays)
} AT_AudioTrack;

typedef enum {
    AT_AUDIO_TRACK_PARAM_PLAY,              // value = slot: its region starts
    AT_AUDIO_TRACK_PARAM_STOP,              // value = slot: the region that started from it fades out
    AT_AUDIO_TRACK_PARAM_STOP_ALL           // every region fades out
} AT_AudioTrackParam;

void audio_track_init(AT_AudioTrack* t, float sample_rate);
void audio_track_free(AT_AudioTrack* t);                        // once the audio thread no longer renders it

// Loads a file (control thread; decoding a long MP3 takes a moment). Returns
// its index; the same path again returns the same index. -1 = failed (error).
int  audio_track_load(AT_AudioTrack* t, const char* path, char* error, size_t error_cap);
int  audio_track_find(const AT_AudioTrack* t, const char* path);   // -1 = not loaded
const AT_Clip* audio_track_clip(const AT_AudioTrack* t, int file);
double audio_track_file_seconds(const AT_AudioTrack* t, int file);

// Sends a region to play. The command carries the caller's engine_at time;
// `at` is that frame (0 = now), for knowing when the slot is free again.
// Returns the slot (for audio_track_stop), -1 = bad file or no free slot.
int  audio_track_play    (AT_AudioTrack* t, const AT_AudioRegion* region, uint64_t at);
void audio_track_stop    (AT_AudioTrack* t, int slot);         // at the caller's engine_at time
void audio_track_stop_all(AT_AudioTrack* t);

// A region as it sounds: which file frame plays at output frame `t` of it
// (for displays and tests; keep_pitch / pitch read grains around it).
double audio_track_region_frame(const AT_AudioRegion* r, const AT_Clip* clip, float sample_rate, double t);

void audio_track_set_param(void* self, int param, float value);    // AT_ParamFn, self = AT_AudioTrack*
extern const AT_SourceType AT_AUDIO_TRACK_SOURCE;
AT_Instrument audio_track_instrument(AT_AudioTrack* t);
extern const char AUDIO_TRACK_HELP[];
