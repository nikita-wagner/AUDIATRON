// ═══════════════════════════════════════════════════════════════════════════
//  tracks/track_list.h — tracks: separate mixer channels, one instrument each
//
//  A track = one engine channel (source → EQ → effects → fader/pan → master)
//  + the instrument that plays on it + its mix settings. Tracks are
//  independent: each has its own sound, EQ, effects and fader, and they are
//  mixed side by side into the master bus.
//
//  This module is the control-thread view of the mixer. It remembers volume,
//  pan, mute, solo and hold per track, works out who is audible when tracks
//  are soloed, and sends the result to the engine.
//
//  Tracks vs. layers: tracks are different instruments played independently.
//  Layers (layers/layer_stack.h) live INSIDE one track and answer the same
//  notes, forming one composite sound.
//
//  Threading: control thread only. Everything reaches the audio thread through
//  engine_* commands.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "engine/engine.h"
#include "instruments/instrument.h"

#define AT_TRACK_MAX AT_ENGINE_CHANNELS

typedef struct {
    AT_Instrument instrument;       // what plays on this track
    uint32_t      channel;          // its engine channel
    float         volume;           // linear, 0..2
    float         pan;              // -1 = left .. +1 = right
    bool          mute;
    bool          solo;
    bool          hold;             // latched: notes keep sounding after their keys are released
} AT_Track;

typedef struct {
    AT_Engine* engine;
    AT_Track   tracks[AT_TRACK_MAX];
    uint32_t   count;
    uint32_t   selected;            // the track the keyboard plays
} AT_TrackList;

void track_list_init(AT_TrackList* l, AT_Engine* engine);

// Gives the instrument the next free engine channel and attaches it.
// Returns the track index, or -1 if every channel is taken.
int track_list_add(AT_TrackList* l, AT_Instrument instrument);

// ─── Selection: which track the keyboard plays ───────────────────────────────

AT_Track* track_list_selected(AT_TrackList* l);                 // NULL if there are no tracks
bool      track_list_select(AT_TrackList* l, uint32_t index);   // false = no such track
void      track_list_select_next(AT_TrackList* l);

// ─── Mix settings of one track ───────────────────────────────────────────────

void track_set_volume(AT_TrackList* l, uint32_t index, float volume);
void track_set_pan   (AT_TrackList* l, uint32_t index, float pan);
void track_set_mute  (AT_TrackList* l, uint32_t index, bool mute);
void track_set_solo  (AT_TrackList* l, uint32_t index, bool solo);

// Latch: while a track is held, releasing a key (or selecting another track)
// does not stop its notes. Switching the hold off stops all of them.
void track_set_hold(AT_TrackList* l, uint32_t index, bool hold);

// True if the track reaches the master bus: not muted, and — if any track is
// soloed — soloed itself. Mute wins over solo.
bool track_is_audible(const AT_TrackList* l, uint32_t index);

// Post-fader peak of the track during the last block (approximate).
float track_peak(const AT_TrackList* l, uint32_t index);
