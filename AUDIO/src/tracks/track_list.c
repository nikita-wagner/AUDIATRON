#include "tracks/track_list.h"

#include "core/at_math.h"

#define DEFAULT_VOLUME 0.8f

void track_list_init(AT_TrackList* l, AT_Engine* engine) {
    memset(l, 0, sizeof *l);
    l->engine = engine;
}

// ─── Who is audible ──────────────────────────────────────────────────────────

static bool any_solo(const AT_TrackList* l) {
    for (uint32_t i = 0; i < l->count; i++)
        if (l->tracks[i].solo) return true;
    return false;
}

bool track_is_audible(const AT_TrackList* l, uint32_t index) {
    if (index >= l->count) return false;
    const AT_Track* t = &l->tracks[index];

    if (t->mute)     return false;
    if (any_solo(l)) return t->solo;
    return true;
}

// One track's mute or solo can change whether OTHER tracks are heard, so the
// engine is told about all of them.
static void send_audibility(const AT_TrackList* l) {
    for (uint32_t i = 0; i < l->count; i++)
        engine_set_channel_mute(l->engine, l->tracks[i].channel, !track_is_audible(l, i));
}

// ─── Adding ──────────────────────────────────────────────────────────────────

int track_list_add(AT_TrackList* l, AT_Instrument instrument) {
    if (l->count == AT_TRACK_MAX) return -1;

    uint32_t  index = l->count++;
    AT_Track* t     = &l->tracks[index];
    *t = (AT_Track){ .instrument = instrument, .channel = index, .volume = DEFAULT_VOLUME };

    engine_set_channel_volume(l->engine, t->channel, t->volume);
    engine_set_channel_pan(l->engine, t->channel, t->pan);
    send_audibility(l);                 // a new track is silent while another one is soloed

    if (instrument.attach) instrument.attach(instrument.self, l->engine, t->channel);
    return (int)index;
}

// ─── Selection ───────────────────────────────────────────────────────────────

AT_Track* track_list_selected(AT_TrackList* l) {
    return l->count ? &l->tracks[l->selected] : NULL;
}

bool track_list_select(AT_TrackList* l, uint32_t index) {
    if (index >= l->count) return false;
    if (index == l->selected) return true;

    // Don't leave the old track's keys hanging. If that track is held, the
    // engine ignores these note-offs and its notes keep sounding.
    AT_Instrument* old = &l->tracks[l->selected].instrument;
    if (old->release_all) old->release_all(old->self);

    l->selected = index;
    return true;
}

void track_list_select_next(AT_TrackList* l) {
    if (l->count > 1) track_list_select(l, (l->selected + 1) % l->count);
}

// ─── Mix settings ────────────────────────────────────────────────────────────

void track_set_volume(AT_TrackList* l, uint32_t index, float volume) {
    if (index >= l->count) return;
    AT_Track* t = &l->tracks[index];
    t->volume = at_clampf(volume, 0.0f, 2.0f);
    engine_set_channel_volume(l->engine, t->channel, t->volume);
}

void track_set_pan(AT_TrackList* l, uint32_t index, float pan) {
    if (index >= l->count) return;
    AT_Track* t = &l->tracks[index];
    t->pan = at_clampf(pan, -1.0f, 1.0f);
    engine_set_channel_pan(l->engine, t->channel, t->pan);
}

void track_set_mute(AT_TrackList* l, uint32_t index, bool mute) {
    if (index >= l->count) return;
    l->tracks[index].mute = mute;
    send_audibility(l);
}

void track_set_solo(AT_TrackList* l, uint32_t index, bool solo) {
    if (index >= l->count) return;
    l->tracks[index].solo = solo;
    send_audibility(l);
}

void track_set_hold(AT_TrackList* l, uint32_t index, bool hold) {
    if (index >= l->count) return;
    AT_Track* t = &l->tracks[index];
    t->hold = hold;
    engine_set_channel_hold(l->engine, t->channel, hold);
}

float track_peak(const AT_TrackList* l, uint32_t index) {
    return index < l->count ? engine_channel_peak(l->engine, l->tracks[index].channel) : 0.0f;
}
