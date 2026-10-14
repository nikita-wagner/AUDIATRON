#include "instruments/sample_player/sample_player.h"

#include <stdio.h>
#include <string.h>

#include "io/audio_file/audio_file.h"

// ─── Load / free ─────────────────────────────────────────────────────────────

static void make_label(AT_SamplePlayer* p, const char* path) {
    const char* name = path;
    for (const char* c = path; *c; c++)
        if (*c == '/' || *c == '\\') name = c + 1;
    snprintf(p->label, sizeof p->label, "%s", name);
}

bool sample_player_init(AT_SamplePlayer* p, const char* path, float sample_rate, char* error, size_t error_cap) {
    memset(p, 0, sizeof *p);

    if (!audio_file_load(path, &p->clip, error, error_cap)) return false;
    p->owns_clip = true;

    p->sample_rate = sample_rate;
    p->speed       = 1.0f;
    make_label(p, path);

    sampler_init(&p->sampler, sample_rate, &p->clip);
    pitch_shift_init(&p->shifter, sample_rate);
    return true;
}

void sample_player_init_copy(AT_SamplePlayer* p, const AT_SamplePlayer* original, float sample_rate) {
    memset(p, 0, sizeof *p);
    p->clip        = original->clip;                        // the same samples, not owned
    p->owns_clip   = false;
    p->sample_rate = sample_rate;
    p->speed       = 1.0f;
    snprintf(p->label, sizeof p->label, "%s", original->label);

    sampler_init(&p->sampler, sample_rate, &p->clip);
    pitch_shift_init(&p->shifter, sample_rate);
}

void sample_player_free(AT_SamplePlayer* p) {
    if (p->owns_clip) audio_clip_free(&p->clip);
    memset(&p->clip, 0, sizeof p->clip);
    memset(&p->sampler, 0, sizeof p->sampler);              // its clip pointer is gone
}

// ─── AT_Instrument adapter ───────────────────────────────────────────────────

// The sampler asks this when its song starts: on a synced track it joins the
// engine's sync clock (audio thread).
static bool player_sync(void* user, double* elapsed) {
    AT_SamplePlayer* p = (AT_SamplePlayer*)user;
    if (!engine_channel_synced(p->engine, p->channel)) return false;
    *elapsed = engine_sync_join(p->engine, p->channel);
    return true;
}

static void player_attach(void* self, AT_Engine* engine, uint32_t channel) {
    AT_SamplePlayer* p = (AT_SamplePlayer*)self;
    p->sampler.sync      = player_sync;
    p->sampler.sync_user = p;

    // Nothing is plugged in yet, so the settings (loop, --pitch, --speed) can still be written directly.
    sampler_set_param(&p->sampler, AT_SAMPLER_PARAM_LOOP, p->loop ? 1.0f : 0.0f);

    engine_set_source(engine, channel, &AT_SAMPLER_SOURCE, &p->sampler);
    engine_set_hook(engine, AT_HOOK_SOURCE, channel, 0, pitch_shift_process, &p->shifter);

    p->engine  = engine;
    p->channel = channel;
}

static bool player_key(void* self, const AT_KeyEvent* ev) {
    return sample_player_key((AT_SamplePlayer*)self, ev);
}

static void player_release_all(void* self) {
    (void)self;         // no keys are "held": a song keeps playing when another track is selected
}

// The offline demo: play the whole file once, looping so a short file fills the render.
static void player_demo_start(void* self) {
    AT_SamplePlayer* p = (AT_SamplePlayer*)self;
    sample_player_set_loop(p, true);
    sample_player_play(p);
}

static void format_time(char* out, size_t cap, double seconds) {
    int total = (int)(seconds + 0.5);
    snprintf(out, cap, "%d:%02d", total / 60, total % 60);
}

// e.g. "song.mp3 play 0:12/3:45 +2st x1.00 tape loop"
static void player_status(void* self, char* out, size_t cap) {
    const AT_SamplePlayer* p = (const AT_SamplePlayer*)self;

    char here[16], length[16], name[16];
    format_time(here,   sizeof here,   sample_player_position_seconds(p));
    format_time(length, sizeof length, audio_clip_seconds(&p->clip));
    snprintf(name, sizeof name, "%.14s", p->label);         // long names are cut to fit the status line

    snprintf(out, cap, "%s %s %s/%s %+.0fst x%.2f %s%s", name,
             sample_player_is_playing(p) ? "play" : "stop", here, length,
             p->pitch, p->speed, p->keep_tempo ? "tempo" : "tape", p->loop ? " loop" : "");
}

AT_Instrument sample_player_instrument(AT_SamplePlayer* p) {
    return (AT_Instrument){
        .name        = "player",
        .help        = SAMPLE_PLAYER_HELP,
        .self        = p,
        .attach      = player_attach,
        .key         = player_key,
        .release_all = player_release_all,
        .status      = player_status,
        .demo_start  = player_demo_start
    };
}
