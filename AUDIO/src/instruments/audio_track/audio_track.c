// ═══════════════════════════════════════════════════════════════════════════
//  instruments/audio_track/audio_track.c — files, slots, control, adapter
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/audio_track/audio_track.h"

#include <stdio.h>
#include <string.h>

#include "io/audio_file/audio_file.h"

const char AUDIO_TRACK_HELP[] =
    "  (no keys: regions of sound files are placed on it in AudioStudio's arrangement)\n";

void audio_track_init(AT_AudioTrack* t, float sample_rate) {
    memset(t, 0, sizeof *t);
    t->sample_rate = sample_rate;
}

void audio_track_free(AT_AudioTrack* t) {
    for (int i = 0; i < AT_AUDIO_TRACK_FILES; i++)
        if (t->files[i].loaded) audio_clip_free(&t->files[i].clip);
    memset(t->files, 0, sizeof t->files);
    memset(t->voices, 0, sizeof t->voices);
}

// ─── Files ───────────────────────────────────────────────────────────────────

int audio_track_find(const AT_AudioTrack* t, const char* path) {
    for (int i = 0; i < AT_AUDIO_TRACK_FILES; i++)
        if (t->files[i].loaded && !_stricmp(t->files[i].path, path)) return i;
    return -1;
}

int audio_track_load(AT_AudioTrack* t, const char* path, char* error, size_t error_cap) {
    const int known = audio_track_find(t, path);
    if (known >= 0) return known;

    int slot = -1;
    for (int i = 0; i < AT_AUDIO_TRACK_FILES && slot < 0; i++)
        if (!t->files[i].loaded) slot = i;
    if (slot < 0) {
        if (error && error_cap) snprintf(error, error_cap, "%d files on the audio track at most", AT_AUDIO_TRACK_FILES);
        return -1;
    }
    AT_AudioFile* f = &t->files[slot];
    if (!audio_file_load(path, &f->clip, error, error_cap)) return -1;

    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(f->label, sizeof f->label, "%s", name);
    snprintf(f->path, sizeof f->path, "%s", path);
    f->loaded = true;                                       // written last: the clip is complete before anyone looks
    return slot;
}

const AT_Clip* audio_track_clip(const AT_AudioTrack* t, int file) {
    if (file < 0 || file >= AT_AUDIO_TRACK_FILES || !t->files[file].loaded) return NULL;
    return &t->files[file].clip;
}

double audio_track_file_seconds(const AT_AudioTrack* t, int file) {
    const AT_Clip* c = audio_track_clip(t, file);
    return c && c->sample_rate ? (double)c->frames / c->sample_rate : 0.0;
}

// ─── Regions ─────────────────────────────────────────────────────────────────

static void send(AT_AudioTrack* t, AT_AudioTrackParam param, float value) {
    if (t->engine) engine_set_param(t->engine, audio_track_set_param, t, (int)param, value);
    else           audio_track_set_param(t, (int)param, value);
}

int audio_track_play(AT_AudioTrack* t, const AT_AudioRegion* region, uint64_t at) {
    if (!audio_track_clip(t, region->file) || region->length_s <= 0.0) return -1;

    // The next slot whose play has happened (or never was): the audio thread
    // copied it into a voice long ago, so it may be written again.
    const uint64_t now = t->engine ? engine_now(t->engine) : 0;
    const uint64_t margin = (uint64_t)(t->sample_rate * 0.1f);
    int slot = -1;
    for (uint32_t k = 0; k < AT_AUDIO_TRACK_SLOTS && slot < 0; k++) {
        const uint32_t i = (t->next_slot + k) % AT_AUDIO_TRACK_SLOTS;
        if (t->slot_due[i] == 0 || t->slot_due[i] + margin < now) slot = (int)i;
    }
    if (slot < 0) return -1;
    t->next_slot = (uint32_t)(slot + 1) % AT_AUDIO_TRACK_SLOTS;

    AT_AudioRegion r = *region;
    if (r.rate < 0.05) r.rate = 0.05;
    if (r.rate > 20.0) r.rate = 20.0;
    if (r.start_s < 0.0) r.start_s = 0.0;
    if (r.pitch < -24.0f) r.pitch = -24.0f;
    if (r.pitch > 24.0f)  r.pitch = 24.0f;
    t->slots[slot]    = r;
    t->slot_due[slot] = (at > now ? at : now) + 1;
    send(t, AT_AUDIO_TRACK_PARAM_PLAY, (float)slot);
    return slot;
}

void audio_track_stop(AT_AudioTrack* t, int slot) {
    if (slot >= 0 && slot < AT_AUDIO_TRACK_SLOTS) send(t, AT_AUDIO_TRACK_PARAM_STOP, (float)slot);
}

void audio_track_stop_all(AT_AudioTrack* t) {
    send(t, AT_AUDIO_TRACK_PARAM_STOP_ALL, 0.0f);
}

double audio_track_region_frame(const AT_AudioRegion* r, const AT_Clip* clip, float sample_rate, double t) {
    const double file_per_out = r->rate * (double)clip->sample_rate / sample_rate;
    const double base         = r->offset_s * clip->sample_rate;
    const double span         = r->length_s * sample_rate * file_per_out;     // file frames the region covers
    const double into         = t * file_per_out;
    return r->reverse ? base + span - into : base + into;
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

static void attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_AudioTrack* t = (AT_AudioTrack*)self;
    t->engine  = engine;
    t->channel = channel;
    engine_set_source(engine, channel, &AT_AUDIO_TRACK_SOURCE, t);
}

static bool key(void* self, const AT_KeyEvent* ev) {
    (void)self;
    (void)ev;
    return false;
}

static void release_all(void* self) {
    (void)self;
}

static void status(void* self, char* out, size_t cap) {
    const AT_AudioTrack* t = (const AT_AudioTrack*)self;
    int files = 0;
    for (int i = 0; i < AT_AUDIO_TRACK_FILES; i++) files += t->files[i].loaded;
    snprintf(out, cap, "%d file%s, %d sounding", files, files == 1 ? "" : "s", (int)t->shown_voices);
}

AT_Instrument audio_track_instrument(AT_AudioTrack* t) {
    return (AT_Instrument){
        .name        = "audio",
        .help        = AUDIO_TRACK_HELP,
        .self        = t,
        .attach      = attach,
        .key         = key,
        .release_all = release_all,
        .status      = status,
    };
}
