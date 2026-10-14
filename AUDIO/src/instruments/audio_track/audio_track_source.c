// ═══════════════════════════════════════════════════════════════════════════
//  instruments/audio_track/audio_track_source.c — rendering regions (AUDIO THREAD)
//
//  Per voice and output frame t (frames into the region):
//    tape (no keep_pitch, no pitch)   the file frame audio_track_region_frame(t),
//                                     read with 4-point cubic interpolation
//    keep_pitch / pitch               two grains, half a window apart, each read
//                                     from where the region stood when it began
//                                     (its anchor) at the pitch's speed, with a
//                                     sine² window: the windows add up to 1
//  then the region's gain, its fade in / out (at least 3 ms each, so a cut in
//  the middle of a sound does not click) and the voice's start / stop declick.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/audio_track/audio_track.h"

#include <math.h>
#include <string.h>

#define DECLICK_S  0.003
#define GRAIN_S    0.060
#define PI_D       3.14159265358979323846

static float sample(const AT_Clip* c, int64_t frame, uint32_t ch) {
    if (frame < 0 || frame >= (int64_t)c->frames) return 0.0f;
    if (ch >= c->channels) ch = c->channels - 1;
    return c->data[frame * c->channels + ch] * (1.0f / 32768.0f);
}

// 4-point cubic (Hermite) between frames.
static float read_at(const AT_Clip* c, double pos, uint32_t ch) {
    const double  fl = floor(pos);
    const int64_t i  = (int64_t)fl;
    const float   x  = (float)(pos - fl);
    const float   y0 = sample(c, i - 1, ch), y1 = sample(c, i, ch), y2 = sample(c, i + 1, ch), y3 = sample(c, i + 2, ch);
    const float   c1 = 0.5f * (y2 - y0);
    const float   c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float   c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + y1;
}

static void start_voice(AT_AudioTrack* t, int slot) {
    const AT_AudioRegion* r    = &t->slots[slot];
    const AT_Clip*        clip = audio_track_clip(t, r->file);
    if (!clip || clip->frames < 4 || !clip->sample_rate) return;

    // a free voice, else the one closest to its end
    AT_AudioVoice* v = NULL;
    for (int i = 0; i < AT_AUDIO_TRACK_VOICES && !v; i++)
        if (!t->voices[i].active) v = &t->voices[i];
    if (!v) {
        v = &t->voices[0];
        for (int i = 1; i < AT_AUDIO_TRACK_VOICES; i++)
            if (t->voices[i].end - t->voices[i].t < v->end - v->t) v = &t->voices[i];
    }
    memset(v, 0, sizeof *v);
    v->active = true;
    v->slot   = slot;
    v->region = *r;
    v->clip   = clip;
    v->end    = r->length_s * t->sample_rate;
    v->t      = r->start_s * t->sample_rate;

    // the two grains as if they had been running: one starting now, one half way through
    const double window = GRAIN_S * t->sample_rate;
    const double here   = audio_track_region_frame(r, clip, t->sample_rate, v->t);
    const double grain  = (r->keep_pitch ? 1.0 : r->rate) * pow(2.0, r->pitch / 12.0) * clip->sample_rate / t->sample_rate;
    const double dir    = r->reverse ? -1.0 : 1.0;
    v->grain_t[0]      = 0.0;
    v->grain_anchor[0] = here;
    v->grain_t[1]      = window * 0.5;
    v->grain_anchor[1] = here - dir * grain * window * 0.5;
}

void audio_track_set_param(void* self, int param, float value) {
    AT_AudioTrack* t    = (AT_AudioTrack*)self;
    const int      slot = (int)(value + 0.5f);
    switch ((AT_AudioTrackParam)param) {
    case AT_AUDIO_TRACK_PARAM_PLAY:
        if (slot >= 0 && slot < AT_AUDIO_TRACK_SLOTS) start_voice(t, slot);
        break;
    case AT_AUDIO_TRACK_PARAM_STOP:
        for (int i = 0; i < AT_AUDIO_TRACK_VOICES; i++)
            if (t->voices[i].active && t->voices[i].slot == slot) t->voices[i].stopping = true;
        break;
    case AT_AUDIO_TRACK_PARAM_STOP_ALL:
        for (int i = 0; i < AT_AUDIO_TRACK_VOICES; i++) t->voices[i].stopping = true;
        break;
    }
}

static void render_voice(AT_AudioTrack* t, AT_AudioVoice* v, AT_Buffer* out) {
    const AT_AudioRegion* r      = &v->region;
    const AT_Clip*        clip   = v->clip;
    const float           sr     = t->sample_rate;
    const double          fade_in  = fmax(r->fade_in_s, DECLICK_S) * sr;
    const double          fade_out = fmax(r->fade_out_s, DECLICK_S) * sr;
    const float           env_step = (float)(1.0 / (DECLICK_S * sr));
    const bool            granular = r->keep_pitch || fabsf(r->pitch) > 1e-4f;
    const double          window   = GRAIN_S * sr;
    const double          grain    = (r->keep_pitch ? 1.0 : r->rate) * pow(2.0, r->pitch / 12.0) * clip->sample_rate / sr;
    const double          dir      = r->reverse ? -1.0 : 1.0;
    const uint32_t        chans    = out->channels < 2 ? out->channels : 2;

    for (uint32_t i = 0; i < out->frames; i++) {
        if (v->t >= v->end) { v->active = false; return; }

        // envelope: the voice's declick, the region's fades, its gain
        if (v->stopping) {
            v->env -= env_step;
            if (v->env <= 0.0f) { v->active = false; return; }
        } else if (v->env < 1.0f) {
            v->env = v->env + env_step > 1.0f ? 1.0f : v->env + env_step;
        }
        double shape = 1.0;
        if (v->t < fade_in)          shape = v->t / fade_in;
        if (v->end - v->t < fade_out) shape = fmin(shape, (v->end - v->t) / fade_out);
        const float g = (float)(shape * v->env * r->gain);

        float l, rr;
        if (!granular) {
            const double pos = audio_track_region_frame(r, clip, sr, v->t);
            l  = read_at(clip, pos, 0);
            rr = clip->channels > 1 ? read_at(clip, pos, 1) : l;
        } else {
            l = rr = 0.0f;
            for (int k = 0; k < 2; k++) {
                if (v->grain_t[k] >= window) {                  // a new grain from where the region is now
                    v->grain_t[k]     -= window;
                    v->grain_anchor[k] = audio_track_region_frame(r, clip, sr, v->t) - dir * grain * v->grain_t[k];
                }
                const double x   = v->grain_t[k] / window;
                const float  w   = (float)(sin(PI_D * x) * sin(PI_D * x));
                const double pos = v->grain_anchor[k] + dir * grain * v->grain_t[k];
                const float  a   = read_at(clip, pos, 0);
                l  += w * a;
                rr += w * (clip->channels > 1 ? read_at(clip, pos, 1) : a);
                v->grain_t[k] += 1.0;
            }
        }
        if (chans == 1) out->ch[0][i] += 0.5f * (l + rr) * g;
        else {
            out->ch[0][i] += l * g;
            out->ch[1][i] += rr * g;
        }
        v->t += 1.0;
    }
}

static void render(AT_Buffer* out, void* self) {
    AT_AudioTrack* t = (AT_AudioTrack*)self;
    int sounding = 0;
    for (int i = 0; i < AT_AUDIO_TRACK_VOICES; i++)
        if (t->voices[i].active) {
            render_voice(t, &t->voices[i], out);
            sounding += t->voices[i].active;
        }
    t->shown_voices = sounding;
}

static void all_notes_off(void* self) {
    audio_track_set_param(self, AT_AUDIO_TRACK_PARAM_STOP_ALL, 0.0f);   // Space / panic stops the regions too
}

const AT_SourceType AT_AUDIO_TRACK_SOURCE = {
    .name          = "audio",
    .render        = render,
    .all_notes_off = all_notes_off,
};
