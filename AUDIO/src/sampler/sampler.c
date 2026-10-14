#include "sampler/sampler.h"

#include "core/at_math.h"

#define FADE_SECONDS   0.005f
#define SILENT         1e-4f
#define RATE_MIN       0.05f
#define RATE_MAX       8.0f
#define REGION_MIN     0.0001f      // the shortest region, as a fraction of the clip

// Markers in AT_SamplerVoice.note for voices that are not played from the keyboard.
#define NOTE_TRANSPORT (-1000)
#define NOTE_ORPHAN    (-1001)      // a voice that is fading out and must not be addressed any more

// ─── Setup ───────────────────────────────────────────────────────────────────

void sampler_init(AT_Sampler* s, float sample_rate, const AT_Clip* clip) {
    memset(s, 0, sizeof *s);
    s->clip        = clip;
    s->rate        = 1.0f;
    s->gain        = 0.8f;
    s->root_note   = AT_SAMPLER_ROOT;
    s->region_from    = 0.0f;
    s->region_to      = 1.0f;
    s->region_from_ms = -1.0f;
    s->region_to_ms   = -1.0f;
    s->sample_rate = sample_rate;
    s->fade_coef   = at_smoothing_coef(FADE_SECONDS, sample_rate);
}

void sampler_set_clip(AT_Sampler* s, const AT_Clip* clip) {
    memset(s->voices, 0, sizeof s->voices);
    s->clip           = clip;
    s->start_position = 0.0;
    s->position_frames = 0;
    s->playing         = 0;
}

// ─── Region ──────────────────────────────────────────────────────────────────

// First and end frame of the part that plays (the clip must be set), from the
// milliseconds where they are set, else from the fractions. At least one frame.
static void region_frames(const AT_Sampler* s, double* first, double* last) {
    const double n  = (double)s->clip->frames;
    const double ms = (double)s->clip->sample_rate / 1000.0;
    double a = s->region_from_ms >= 0.0f ? (double)s->region_from_ms * ms : (double)s->region_from * n;
    double b = s->region_to_ms   >= 0.0f ? (double)s->region_to_ms   * ms : (double)s->region_to   * n;
    if (a > n - 1.0) a = n - 1.0;
    if (a < 0.0)     a = 0.0;
    if (b > n)       b = n;
    if (b < a + 1.0) b = a + 1.0;
    *first = a;
    *last  = b;
}

static double region_start(const AT_Sampler* s) { double a, b; region_frames(s, &a, &b); return a; }
static double region_end  (const AT_Sampler* s) { double a, b; region_frames(s, &a, &b); return b; }

void sampler_set_region(AT_Sampler* s, float from, float to) {
    s->region_from    = at_clampf(from, 0.0f, 1.0f - REGION_MIN);
    s->region_to      = at_clampf(to, s->region_from + REGION_MIN, 1.0f);
    s->region_from_ms = -1.0f;
    s->region_to_ms   = -1.0f;
}

void sampler_set_region_ms(AT_Sampler* s, float from_ms, float to_ms) {
    s->region_from_ms = from_ms < 0.0f ? 0.0f : from_ms;
    s->region_to_ms   = to_ms > s->region_from_ms ? to_ms : s->region_from_ms;   // region_frames keeps one frame
}

// ─── Voices ──────────────────────────────────────────────────────────────────

static AT_SamplerVoice* find_voice(AT_Sampler* s, int note) {
    for (int i = 0; i < AT_SAMPLER_VOICES; i++)
        if (s->voices[i].active && s->voices[i].note == note && !s->voices[i].releasing) return &s->voices[i];
    return NULL;
}

// Lets a voice fade out and takes it out of reach of note_off / transport commands.
static void orphan(AT_SamplerVoice* v) {
    v->releasing = true;
    v->paused    = false;
    v->note      = NOTE_ORPHAN;
}

// A free voice; else steal one. Fading voices go first, then the oldest note.
// The transport voice is only taken if nothing else can be.
static AT_SamplerVoice* allocate_voice(AT_Sampler* s) {
    AT_SamplerVoice* best = NULL;
    int              best_rank = -1;

    for (int i = 0; i < AT_SAMPLER_VOICES; i++) {
        AT_SamplerVoice* v = &s->voices[i];
        if (!v->active) return v;

        int rank = v->releasing ? 3 : v->note == NOTE_TRANSPORT ? 0 : 1;
        if (rank > best_rank || (rank == best_rank && v->age < best->age)) { best = v; best_rank = rank; }
    }
    return best;
}

static AT_SamplerVoice* start_voice(AT_Sampler* s, int note, float velocity, double position, float note_ratio) {
    AT_SamplerVoice* v = allocate_voice(s);
    memset(v, 0, sizeof *v);
    v->active     = true;
    v->note       = note;
    v->velocity   = velocity;
    v->note_ratio = note_ratio;
    v->position   = position;
    v->env        = 0.0f;                   // fades in: no click
    v->age        = ++s->age_counter;
    return v;
}

// ─── Notes ───────────────────────────────────────────────────────────────────

void sampler_note_on(AT_Sampler* s, int note, float velocity) {
    if (!s->clip || s->clip->frames == 0) return;

    // Playing the same note again: the old one fades out, the new one fades in.
    AT_SamplerVoice* old = find_voice(s, note);
    if (old) orphan(old);

    float ratio = powf(2.0f, (float)(note - s->root_note) / 12.0f);
    start_voice(s, note, at_clampf(velocity, 0.0f, 1.0f), region_start(s), ratio);
}

void sampler_note_off(AT_Sampler* s, int note) {
    if (!s->gate) return;                   // one-shot: the note plays to the end of the clip

    AT_SamplerVoice* v = find_voice(s, note);
    if (v) v->releasing = true;
}

void sampler_all_notes_off(AT_Sampler* s) {
    for (int i = 0; i < AT_SAMPLER_VOICES; i++)
        if (s->voices[i].active) s->voices[i].releasing = true;
    s->start_position = 0.0;
}

// ─── Transport ───────────────────────────────────────────────────────────────

static AT_SamplerVoice* transport_voice(AT_Sampler* s) {
    for (int i = 0; i < AT_SAMPLER_VOICES; i++)
        if (s->voices[i].active && s->voices[i].note == NOTE_TRANSPORT && !s->voices[i].releasing) return &s->voices[i];
    return NULL;
}

static void transport_play(AT_Sampler* s) {
    if (!s->clip || s->clip->frames == 0) return;

    AT_SamplerVoice* v = transport_voice(s);
    if (v) { v->paused = false; return; }                   // resume

    // a rewind (0) or a place outside a changed region starts at the region
    double position = s->start_position;
    if (position < region_start(s) || position >= region_end(s)) position = region_start(s);
    start_voice(s, NOTE_TRANSPORT, 1.0f, position, 1.0f);
}

static void transport_stop(AT_Sampler* s) {
    AT_SamplerVoice* v = transport_voice(s);
    if (v) orphan(v);
    s->start_position = 0.0;
}

static bool timed(const AT_Sampler* s) { return s->delay_ms > 0.0f || s->length_ms > 0.0f; }

void sampler_play_elapsed(AT_Sampler* s, double elapsed) {
    if (!s->clip || s->clip->frames == 0) return;

    transport_stop(s);                                      // a running one fades out
    double start, end;
    region_frames(s, &start, &end);
    AT_SamplerVoice* v = start_voice(s, NOTE_TRANSPORT, 1.0f, start, 1.0f);
    v->elapsed = elapsed;                                   // timed: render works out where it is
    if (!timed(s)) {
        double step = (double)s->rate * (double)s->clip->sample_rate / (double)s->sample_rate;
        v->position = start + fmod(elapsed * step, end - start);
    }
}

void sampler_play_at(AT_Sampler* s, double frame) {
    if (!s->clip || s->clip->frames == 0) return;

    transport_stop(s);                                      // a running one fades out
    if (frame < region_start(s) || frame >= region_end(s)) frame = region_start(s);
    start_voice(s, NOTE_TRANSPORT, 1.0f, frame, 1.0f);
}

static void transport_seek(AT_Sampler* s, double seconds) {
    if (!s->clip || s->clip->frames == 0) return;

    double frame = seconds * (double)s->clip->sample_rate;
    double first = region_start(s), last = region_end(s) - 1.0;
    if (frame > last)  frame = last;
    if (frame < first) frame = first;

    AT_SamplerVoice* v = transport_voice(s);
    if (v) v->position = frame;                             // a jump, not a click: the fade is on the volume
    else   s->start_position = frame;
}

// ─── Parameters ──────────────────────────────────────────────────────────────

void sampler_set_param(void* self, int param, float value) {
    AT_Sampler* s = (AT_Sampler*)self;

    switch ((AT_SamplerParam)param) {
    case AT_SAMPLER_PARAM_PLAY: {
        double elapsed = 0.0;
        if (value < 0.5f && !transport_voice(s) && s->sync && s->sync(s->sync_user, &elapsed))
            sampler_play_elapsed(s, elapsed);               // joins the sync clock in phase
        else
            transport_play(s);
        break;
    }
    case AT_SAMPLER_PARAM_PAUSE: {
        AT_SamplerVoice* v = transport_voice(s);
        if (v) v->paused = value != 0.0f;
        break;
    }
    case AT_SAMPLER_PARAM_STOP:  transport_stop(s);                                   break;
    case AT_SAMPLER_PARAM_SEEK:  transport_seek(s, (double)value);                    break;
    case AT_SAMPLER_PARAM_RATE:  s->rate = at_clampf(value, RATE_MIN, RATE_MAX);      break;
    case AT_SAMPLER_PARAM_LOOP:  s->loop = value != 0.0f;                             break;
    case AT_SAMPLER_PARAM_GAIN:  s->gain = at_clampf(value, 0.0f, 4.0f);              break;
    case AT_SAMPLER_PARAM_GATE:  s->gate = value != 0.0f;                             break;
    case AT_SAMPLER_PARAM_ROOT:  s->root_note = (int)value;                           break;
    case AT_SAMPLER_PARAM_REGION_FROM:
        s->region_from    = at_clampf(value, 0.0f, 1.0f - REGION_MIN);
        s->region_to      = at_clampf(s->region_to, s->region_from + REGION_MIN, 1.0f);
        s->region_from_ms = -1.0f;
        break;
    case AT_SAMPLER_PARAM_REGION_TO:
        s->region_to      = at_clampf(value, s->region_from + REGION_MIN, 1.0f);
        s->region_to_ms   = -1.0f;
        break;
    case AT_SAMPLER_PARAM_REGION_FROM_MS: s->region_from_ms = value < 0.0f ? -1.0f : value;   break;
    case AT_SAMPLER_PARAM_REGION_TO_MS:   s->region_to_ms   = value < 0.0f ? -1.0f : value;   break;
    case AT_SAMPLER_PARAM_DELAY_MS:       s->delay_ms       = value < 0.0f ? 0.0f : value;    break;
    case AT_SAMPLER_PARAM_LENGTH_MS:      s->length_ms      = value < 0.0f ? 0.0f : value;    break;
    }
}

// ─── Reading the clip ────────────────────────────────────────────────────────

// Sample `index` of channel `c`. Outside the clip: wrap when looping, else the
// nearest edge sample (so interpolation at the very end stays quiet).
static inline float clip_sample(const AT_Clip* clip, int64_t index, uint32_t c, bool loop) {
    int64_t n = (int64_t)clip->frames;
    if (index < 0 || index >= n) {
        if (loop) { index %= n; if (index < 0) index += n; }
        else      index = index < 0 ? 0 : n - 1;
    }
    return (float)clip->data[(size_t)index * clip->channels + c];
}

// Catmull-Rom spline through y1..y2 at t (0..1), using y0 and y3 for the slope.
static inline float hermite(float y0, float y1, float y2, float y3, float t) {
    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static inline void read_stereo(const AT_Clip* clip, double position, bool loop, float* l, float* r) {
    int64_t i = (int64_t)position;
    float   t = (float)(position - (double)i);

    float left = hermite(clip_sample(clip, i - 1, 0, loop), clip_sample(clip, i, 0, loop),
                         clip_sample(clip, i + 1, 0, loop), clip_sample(clip, i + 2, 0, loop), t);
    if (clip->channels == 1) { *l = *r = left; return; }

    *l = left;
    *r = hermite(clip_sample(clip, i - 1, 1, loop), clip_sample(clip, i, 1, loop),
                 clip_sample(clip, i + 1, 1, loop), clip_sample(clip, i + 2, 1, loop), t);
}

// ─── Render ──────────────────────────────────────────────────────────────────

// How loud the voice may be at `position` near a cut edge: 0 at the edge,
// rising to 1 over FADE_SECONDS of output. A cut in the middle of a sound would
// otherwise click. Edges of the whole clip are left alone (a file's own start
// and end are already quiet, and a full-clip loop stays as it was).
static inline float edge_gain(double position, double start, double end, double fade_frames,
                              bool fade_start, bool fade_end) {
    double gain = 1.0;
    if (fade_end)   { double g = (end - position)   / fade_frames; if (g < gain) gain = g; }
    if (fade_start) { double g = (position - start) / fade_frames; if (g < gain) gain = g; }
    return gain < 0.0 ? 0.0f : (float)gain;
}

// A voice with delay / length timing. Its time is counted in output frames
// (v->elapsed): silent for `delay`, then the region from its start, for
// `length` (cut with a fade, or silent after the region); with loop again
// every `length`.
static void render_timed(AT_Sampler* s, AT_SamplerVoice* v, AT_Buffer* out) {
    const AT_Clip* clip   = s->clip;
    const double   step   = (double)s->rate * (double)v->note_ratio * (double)clip->sample_rate / (double)s->sample_rate;
    double         start, end;
    region_frames(s, &start, &end);
    const double   per_ms = (double)s->sample_rate / 1000.0;
    const double   delay  = (double)s->delay_ms * per_ms;
    const double   region = (end - start) / step;                       // output frames the region lasts
    const double   length = s->length_ms > 0.0f ? (double)s->length_ms * per_ms : region;
    const bool     cut_by_length = length < region;
    const double   fade   = (double)FADE_SECONDS * (double)s->sample_rate;   // output frames
    const float    amp    = s->gain * v->velocity * (1.0f / 32768.0f);
    float*         left   = out->ch[0];
    float*         right  = out->ch[out->channels > 1 ? 1 : 0];

    for (uint32_t i = 0; i < out->frames; i++) {
        float target = (v->releasing || v->paused) ? 0.0f : 1.0f;
        v->env += (target - v->env) * s->fade_coef;
        if (v->env < SILENT && target == 0.0f) {
            if (v->releasing) v->active = false;
            return;                                         // paused and faded out: hold the place
        }
        if (v->paused) continue;

        double t = v->elapsed;
        v->elapsed += 1.0;
        if (t < delay) continue;                            // still waiting

        double u = t - delay;
        double k = floor(u / length);                       // which repetition
        double w = u - k * length;                          // output frames into it
        if (!s->loop && k >= 1.0) { v->active = false; if (v->note == NOTE_TRANSPORT) s->start_position = 0.0; return; }

        double pos = start + w * step;
        if (pos >= end) {                                   // the region is over: silence until the next repetition
            if (!s->loop) { v->active = false; if (v->note == NOTE_TRANSPORT) s->start_position = 0.0; return; }
            continue;
        }
        v->position = pos;

        double g = 1.0;                                     // 5 ms ramps, so no edge clicks
        if (w < fade)                                   g = w / fade;
        if (cut_by_length && length - w < fade)         { double e = (length - w) / fade; if (e < g) g = e; }
        // the part's end always fades: silence follows it here (also at a file's own end)
        if ((end - pos) / step < fade)                  { double e = (end - pos) / step / fade; if (e < g) g = e; }

        float l, r;
        read_stereo(clip, pos, false, &l, &r);
        float gain = amp * v->env * (float)g;
        left[i] += l * gain;
        if (right != left) right[i] += r * gain;
    }
}

static void render_voice(AT_Sampler* s, AT_SamplerVoice* v, AT_Buffer* out) {
    if (timed(s)) { render_timed(s, v, out); return; }

    const AT_Clip* clip   = s->clip;
    const double   step   = (double)s->rate * (double)v->note_ratio * (double)clip->sample_rate / (double)s->sample_rate;
    double         start, end;
    region_frames(s, &start, &end);
    const float    amp    = s->gain * v->velocity * (1.0f / 32768.0f);
    float*         left   = out->ch[0];
    float*         right  = out->ch[out->channels > 1 ? 1 : 0];

    // Fades at cut edges: the end of a cut region; with loop, both sides of the seam.
    const bool   cut_end     = end < (double)clip->frames;
    const bool   cut         = start > 0.0 || cut_end;
    const bool   fade_end    = cut_end || (s->loop && cut);
    const bool   fade_start  = s->loop && cut;
    const double fade_frames = (double)FADE_SECONDS * (double)s->sample_rate * step;    // in clip frames

    for (uint32_t i = 0; i < out->frames; i++) {
        float target = (v->releasing || v->paused) ? 0.0f : 1.0f;
        v->env += (target - v->env) * s->fade_coef;

        if (v->env < SILENT && target == 0.0f) {
            if (v->releasing) { v->active = false; return; }
            return;                         // paused and faded out: hold the place, cost nothing
        }

        float gain = amp * v->env;
        if (fade_end || fade_start) gain *= edge_gain(v->position, start, end, fade_frames, fade_start, fade_end);

        float l, r;
        read_stereo(clip, v->position, s->loop, &l, &r);
        left[i] += l * gain;
        if (right != left) right[i] += r * gain;

        v->position += step;
        if (v->position >= end) {
            if (s->loop) {
                v->position = start + fmod(v->position - start, end - start);
            } else {
                v->active = false;          // the clip (or its region) is over
                if (v->note == NOTE_TRANSPORT) s->start_position = 0.0;     // a song rewinds
                return;
            }
        }
    }
}

void sampler_render(AT_Sampler* s, AT_Buffer* out) {
    if (s->clip && s->clip->frames > 0 && s->clip->data) {
        for (int i = 0; i < AT_SAMPLER_VOICES; i++)
            if (s->voices[i].active) render_voice(s, &s->voices[i], out);
    }

    // readouts for the control thread
    AT_SamplerVoice* t = transport_voice(s);
    if (t) {
        s->position_frames = (int32_t)t->position;
        s->playing         = !t->paused;
    } else {
        // a transport voice that ran off the end of the clip rewinds
        s->position_frames = (int32_t)s->start_position;
        s->playing         = 0;
    }
}
