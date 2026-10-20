// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_control.c — per-pad settings and hits
//
//  Where each setting of a pad lives on the audio side:
//
//      hit               engine_note_on(36 + pad) → the layer stack routes it
//                        to the pad's layer only
//      loop start/stop   the sampler's transport voice, started where the
//                        clock says (drumpad_clock.c)
//      sync / free       which clock the pad runs on
//      pitch, tape       sampler rate = 2^(pitch/12)   shifter ratio = 1
//      pitch, keep tempo sampler rate = 1              shifter ratio = 2^(pitch/12)
//      volume, pan       the pad's layer strip in the stack
//      cut               the sampler's region (from..to of the file)
//      effects           the pad's own insert (drumpad_effects.c)
//
//  Everything runs on the control thread and reaches the audio thread through
//  engine_set_param; before attach the values are written directly.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <math.h>

#include "core/at_math.h"

// ─── Commands that touch several sampler settings at once (audio thread) ─────

typedef enum {
    CMD_LOOP_ON,            // loop flag on; one-shot notes stop (they would loop forever)
    CMD_STOP                // every voice of the pad fades out
} PadCommand;

// Ends the pad's one-shot note. Every hit reaches the sampler as its root note
// (the layer transposes it), and a note_off only counts while gate is on.
static void cut_notes(AT_Sampler* s) {
    sampler_set_param(s, AT_SAMPLER_PARAM_GATE, 1.0f);
    sampler_note_off(s, AT_SAMPLER_ROOT);
    sampler_set_param(s, AT_SAMPLER_PARAM_GATE, 0.0f);
}

// AT_ParamFn — self = AT_Sampler*, param = PadCommand.
static void pad_command(void* self, int param, float value) {
    AT_Sampler* s = (AT_Sampler*)self;
    (void)value;

    switch ((PadCommand)param) {
    case CMD_LOOP_ON:
        cut_notes(s);
        sampler_set_param(s, AT_SAMPLER_PARAM_LOOP, 1.0f);
        break;
    case CMD_STOP:
        sampler_all_notes_off(s);
        break;
    }
}

// AT_ParamFn — self = AT_Drumpad*, param = pad. The loop (re)starts where the
// clock says: from the top, or in phase with the running synced loops.
static void loop_start_command(void* self, int param, float value) {
    AT_Drumpad* d = (AT_Drumpad*)self;
    AT_Sampler* s = &d->pads[param].sampler;
    (void)value;

    double elapsed = drumpad_clock_loop_start(d, param);
    cut_notes(s);
    sampler_set_param(s, AT_SAMPLER_PARAM_LOOP, 1.0f);
    sampler_play_elapsed(s, elapsed);                       // a running loop of this pad fades out
}

// A running loop starts over where the command lands (a transport starting or looping back): on a synced
// track where the engine's sync clock says (pinned to the song, it is where the song is since its beat 0);
// otherwise from its top, and the drumpad's own clock starts over there too, so every pad restarted on the
// same frame is in phase. A loop stopped meanwhile stays stopped.
static void loop_restart_command(void* self, int param, float value) {
    AT_Drumpad* d = (AT_Drumpad*)self;
    AT_DrumPad* p = &d->pads[param];
    AT_Sampler* s = &p->sampler;
    (void)value;
    if (!s->playing) return;

    const uint64_t now     = d->clock.next;
    double         elapsed = 0.0;
    p->own_origin = now;
    if (p->synced && engine_channel_synced(d->engine, d->channel)) {
        elapsed = engine_sync_join(d->engine, d->channel);
        const uint64_t ago = (uint64_t)llround(elapsed);
        d->clock.origin = d->engine->frame_clock >= ago ? d->engine->frame_clock - ago : 0;
    } else if (p->synced) {
        d->clock.origin = now;
    }
    cut_notes(s);
    sampler_set_param(s, AT_SAMPLER_PARAM_LOOP, 1.0f);
    sampler_play_elapsed(s, elapsed);                       // the running loop fades out
}

// AT_ParamFn — self = AT_DrumPad*, value = 1 synced / 0 free.
static void sync_command(void* self, int param, float value) {
    (void)param;
    ((AT_DrumPad*)self)->synced = value != 0.0f;
}

// ─── Sending: directly before attach, through the engine afterwards ──────────

static AT_DrumPad* get_pad(AT_Drumpad* d, int pad) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return NULL;
    return &d->pads[pad];
}

static void send(AT_Drumpad* d, AT_ParamFn fn, void* self, int param, float value) {
    if (d->engine) engine_set_param(d->engine, fn, self, param, value);
    else           fn(self, param, value);
}

static void send_layer(AT_Drumpad* d, const AT_DrumPad* p, AT_LayerParam id, float value) {
    if (p->layer < 0) return;                   // not in the stack yet: applied when it is built
    send(d, layer_stack_set_param, &d->stack, AT_LAYER_PARAM(p->layer, id), value);
}

static void apply_pitch(AT_Drumpad* d, AT_DrumPad* p) {
    float ratio = powf(2.0f, p->pitch / 12.0f);

    send(d, sampler_set_param,     &p->sampler, AT_SAMPLER_PARAM_RATE,       p->keep_tempo ? 1.0f : ratio);
    send(d, pitch_shift_set_param, &p->shifter, AT_PITCH_SHIFT_PARAM_RATIO, p->keep_tempo ? ratio : 1.0f);
}

void drumpad_apply_all(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    apply_pitch(d, p);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_LOOP, p->loop ? 1.0f : 0.0f);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_FROM, p->cut_from);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_TO,   p->cut_to);
    if (p->cut_in_ms) {
        send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_FROM_MS, p->cut_from_ms);
        send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_TO_MS,   p->cut_to_ms);
    }
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_DELAY_MS,  p->wait_ms);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_LENGTH_MS, p->play_ms);
    send_layer(d, p, AT_LAYER_PARAM_VOLUME, p->volume);
    send_layer(d, p, AT_LAYER_PARAM_PAN,    p->pan);
    send(d, sync_command, p, 0, p->sync ? 1.0f : 0.0f);
}

bool drumpad_set_timing(AT_Drumpad* d, int pad, float wait_ms, float play_ms) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p || !(wait_ms >= 0.0f) || !(play_ms >= 0.0f)) return false;      // also rejects NaN

    p->wait_ms = wait_ms;
    p->play_ms = play_ms;
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_DELAY_MS,  wait_ms);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_LENGTH_MS, play_ms);
    drumpad_emit(d, AT_PAD_EV_TIMING, pad, 0, wait_ms, play_ms);
    return true;
}

void drumpad_set_sync(AT_Drumpad* d, int pad, bool sync) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->sync = sync;
    send(d, sync_command, p, 0, sync ? 1.0f : 0.0f);
    drumpad_emit(d, AT_PAD_EV_SYNC, pad, 0, sync ? 1.0f : 0.0f, 0.0f);
}

// ─── Playing ─────────────────────────────────────────────────────────────────

void drumpad_hit(AT_Drumpad* d, int pad, float velocity) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    // Without an engine (a replay renders its drumpad itself) the hit goes
    // straight into the source, through the same layer and clock as a note.
    if (d->engine)        engine_note_on(d->engine, d->channel, AT_DRUMPAD_FIRST_NOTE + pad, velocity);
    else if (d->prepared) AT_DRUMPAD_SOURCE.note_on(d, AT_DRUMPAD_FIRST_NOTE + pad, velocity);
    drumpad_emit(d, AT_PAD_EV_HIT, pad, 0, velocity, 0.0f);
}

void drumpad_loop_start(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->loop = true;
    send(d, loop_start_command, d, pad, 0.0f);
    drumpad_emit(d, AT_PAD_EV_LOOP_START, pad, 0, 0.0f, 0.0f);
}

void drumpad_loop_restart(AT_Drumpad* d, int pad) {
    if (!get_pad(d, pad)) return;
    send(d, loop_restart_command, d, pad, 0.0f);
}

void drumpad_loop_stop(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    send(d, pad_command, &p->sampler, CMD_STOP, 0.0f);
    drumpad_emit(d, AT_PAD_EV_LOOP_STOP, pad, 0, 0.0f, 0.0f);
}

bool drumpad_is_looping(const AT_Drumpad* d, int pad) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return false;
    return d->pads[pad].sampler.playing != 0;   // the transport voice = the loop
}

// ─── Settings ────────────────────────────────────────────────────────────────

void drumpad_set_pitch(AT_Drumpad* d, int pad, float semitones) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->pitch = at_clampf(semitones, AT_DRUMPAD_PITCH_MIN, AT_DRUMPAD_PITCH_MAX);
    apply_pitch(d, p);
    drumpad_emit(d, AT_PAD_EV_PITCH, pad, 0, p->pitch, 0.0f);
}

void drumpad_set_keep_tempo(AT_Drumpad* d, int pad, bool keep_tempo) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->keep_tempo = keep_tempo;
    apply_pitch(d, p);
    drumpad_emit(d, AT_PAD_EV_KEEP_TEMPO, pad, 0, keep_tempo ? 1.0f : 0.0f, 0.0f);
}

void drumpad_set_loop(AT_Drumpad* d, int pad, bool loop) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->loop = loop;
    if (loop) send(d, pad_command, &p->sampler, CMD_LOOP_ON, 0.0f);
    else      send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_LOOP, 0.0f);    // plays to the end of the file
    drumpad_emit(d, AT_PAD_EV_LOOP, pad, 0, loop ? 1.0f : 0.0f, 0.0f);
}

void drumpad_set_volume(AT_Drumpad* d, int pad, float volume) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->volume = at_clampf(volume, 0.0f, AT_DRUMPAD_VOLUME_MAX);
    send_layer(d, p, AT_LAYER_PARAM_VOLUME, p->volume);
    drumpad_emit(d, AT_PAD_EV_VOLUME, pad, 0, p->volume, 0.0f);
}

void drumpad_set_pan(AT_Drumpad* d, int pad, float pan) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->pan = at_clampf(pan, -1.0f, 1.0f);
    send_layer(d, p, AT_LAYER_PARAM_PAN, p->pan);
    drumpad_emit(d, AT_PAD_EV_PAN, pad, 0, p->pan, 0.0f);
}

void drumpad_reset(AT_Drumpad* d, int pad) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p) return;

    p->pitch      = 0.0f;
    p->keep_tempo = false;
    p->volume     = 1.0f;
    p->pan        = 0.0f;
    drumpad_apply_all(d, pad);
    d->quiet++;                                             // one "reset" event, not one per effect
    for (int i = 0; i < AT_PAD_FX_COUNT; i++)
        if (p->fx[i]) drumpad_set_fx(d, pad, (AT_PadFx)i, false);
    d->quiet--;
    drumpad_emit(d, AT_PAD_EV_RESET, pad, 0, 0.0f, 0.0f);
}

bool drumpad_set_cut(AT_Drumpad* d, int pad, float from, float to) {
    AT_DrumPad* p = get_pad(d, pad);
    if (!p || !(from >= 0.0f && from < to && to <= 1.0f)) return false;    // also rejects NaN

    p->cut_from  = from;
    p->cut_to    = to;
    p->cut_in_ms = false;
    // Sent as two values: the sampler keeps from < to after each, and both
    // arrive before the next block, so the order does not matter.
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_FROM, from);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_TO,   to);
    drumpad_emit(d, AT_PAD_EV_CUT, pad, 0, from, to);
    return true;
}

double drumpad_length_ms(const AT_Drumpad* d, int pad) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return 0.0;
    return audio_clip_seconds(&d->pads[pad].clip) * 1000.0;
}

bool drumpad_set_cut_ms(AT_Drumpad* d, int pad, float from_ms, float to_ms) {
    AT_DrumPad* p      = get_pad(d, pad);
    double      length = drumpad_length_ms(d, pad);
    if (!p || !(from_ms >= 0.0f && from_ms < length && to_ms > from_ms)) return false;   // also rejects NaN
    if (to_ms > length) to_ms = (float)length;

    p->cut_in_ms   = true;
    p->cut_from_ms = from_ms;
    p->cut_to_ms   = to_ms;
    p->cut_from    = (float)(from_ms / length);             // for reading only; the milliseconds count
    p->cut_to      = (float)(to_ms   / length);
    // The sampler turns milliseconds into frames itself, exactly; no fraction in between.
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_FROM_MS, from_ms);
    send(d, sampler_set_param, &p->sampler, AT_SAMPLER_PARAM_REGION_TO_MS,   to_ms);
    drumpad_emit(d, AT_PAD_EV_CUT_MS, pad, 0, from_ms, to_ms);
    return true;
}
