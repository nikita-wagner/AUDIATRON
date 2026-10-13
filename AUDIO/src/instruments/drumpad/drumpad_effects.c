// ═══════════════════════════════════════════════════════════════════════════
//  instruments/drumpad/drumpad_effects.c — the effects every pad owns
//
//  Each pad has its own crusher, tremolo and echo. They run in the pad's layer
//  (insert 1, after the pitch shifter), so they change that pad only: an echo
//  on the snare leaves the kick dry, and every pad's echo tail is its own.
//
//  The insert is always plugged in; switching an effect only flips a flag on
//  the audio thread, so nothing has to be re-plugged while the pad sounds.
//
//  Memory: crusher and tremolo are a few bytes. The echo is ~1 MB of delay
//  line, so it is only allocated the first time it is switched on for a pad
//  (on the control thread) and handed to the audio thread through the engine's
//  command queue, like any other state plugged into the engine.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/drumpad/drumpad.h"

#include <stdlib.h>

// The same sound as the app's global effects (app/app_effects.c).
#define CRUSH_DOWNSAMPLE   6
#define CRUSH_BITS         5.0f
#define TREMOLO_HZ         6.0f
#define TREMOLO_DEPTH      0.8f
#define ECHO_MS            350.0f
#define ECHO_FEEDBACK      0.45f
#define ECHO_MIX           0.4f

const char* const AT_PAD_FX_NAMES[AT_PAD_FX_COUNT] = { "crush", "tremolo", "echo" };

// An echo together with the pad it belongs to, so one command can hand it over.
typedef struct {
    AT_Echo     echo;
    AT_DrumPad* pad;
} PadEcho;

void drumpad_fx_init(AT_DrumPad* p, float sample_rate) {
    (void)sample_rate;
    bitcrusher_init(&p->crusher, CRUSH_DOWNSAMPLE, CRUSH_BITS);
    tremolo_init(&p->tremolo, TREMOLO_HZ, TREMOLO_DEPTH);
    p->echo        = NULL;
    p->echo_memory = NULL;
    p->echo_ms     = (int)ECHO_MS;
    for (int i = 0; i < AT_PAD_FX_COUNT; i++) p->fx[i] = p->fx_running[i] = false;
}

void drumpad_fx_free(AT_DrumPad* p) {
    free(p->echo_memory);
    p->echo_memory = NULL;
    p->echo        = NULL;
}

// ─── Audio thread ────────────────────────────────────────────────────────────

void drumpad_fx_process(AT_Buffer* buf, void* user) {
    AT_DrumPad* p = (AT_DrumPad*)user;

    // The effects see the pad's own time base: the shared clock for a synced
    // pad, its own start for a free one (the tremolo's LFO phase follows it).
    AT_Buffer local = *buf;
    uint64_t  origin = drumpad_clock_origin(p);
    local.frame_clock = buf->frame_clock >= origin ? buf->frame_clock - origin : 0;

    if (p->fx_running[AT_PAD_FX_CRUSH])            bitcrusher_process(&local, &p->crusher);
    if (p->fx_running[AT_PAD_FX_TREMOLO])          tremolo_process(&local, &p->tremolo);
    if (p->fx_running[AT_PAD_FX_ECHO] && p->echo)  echo_process(&local, p->echo);
}

// AT_ParamFn — self = PadEcho*: the pad starts using its echo.
static void hand_over_echo(void* self, int param, float value) {
    PadEcho* block = (PadEcho*)self;
    (void)param; (void)value;
    block->pad->echo = &block->echo;
}

// AT_ParamFn — self = AT_DrumPad*, param = AT_PadFx, value = on / off.
static void switch_fx(void* self, int param, float value) {
    AT_DrumPad* p  = (AT_DrumPad*)self;
    bool        on = value != 0.0f;

    // An echo switched on again starts clean, not with the tail it was cut off with.
    if (param == AT_PAD_FX_ECHO && on && !p->fx_running[AT_PAD_FX_ECHO] && p->echo) echo_clear(p->echo);
    p->fx_running[param] = on;
}

// ─── Control thread ──────────────────────────────────────────────────────────

static void send(AT_Drumpad* d, AT_ParamFn fn, void* self, int param, float value) {
    if (d->engine) engine_set_param(d->engine, fn, self, param, value);
    else           fn(self, param, value);
}

bool drumpad_reserve_echo(AT_Drumpad* d, int pad) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return false;
    AT_DrumPad* p = &d->pads[pad];
    if (p->echo_memory) return true;

    PadEcho* block = (PadEcho*)malloc(sizeof *block);
    if (!block) return false;
    echo_init(&block->echo, d->sample_rate, (float)p->echo_ms, ECHO_FEEDBACK, ECHO_MIX);
    block->pad     = p;
    p->echo_memory = block;
    send(d, hand_over_echo, block, 0, 0.0f);                // queued before any switch that follows
    return true;
}

void drumpad_set_echo_ms(AT_Drumpad* d, int pad, int ms) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return;
    AT_DrumPad* p = &d->pads[pad];

    int max = echo_max_ms_at(d->sample_rate);                // the echo's delay line
    p->echo_ms = ms < 1 ? 1 : ms > max ? max : ms;

    if (p->echo_memory)                                     // made already: it glides to the new time
        send(d, echo_set_param, &((PadEcho*)p->echo_memory)->echo, AT_ECHO_PARAM_TIME, (float)p->echo_ms);
    drumpad_emit(d, AT_PAD_EV_ECHO_MS, pad, 0, (float)p->echo_ms, 0.0f);
}

bool drumpad_set_fx(AT_Drumpad* d, int pad, AT_PadFx fx, bool on) {
    if (pad < 0 || pad >= AT_DRUMPAD_PADS || !d->pads[pad].loaded) return false;
    if ((int)fx < 0 || fx >= AT_PAD_FX_COUNT) return false;
    AT_DrumPad* p = &d->pads[pad];

    if (fx == AT_PAD_FX_ECHO && on && !drumpad_reserve_echo(d, pad)) return false;

    p->fx[fx] = on;
    send(d, switch_fx, p, (int)fx, on ? 1.0f : 0.0f);
    drumpad_emit(d, AT_PAD_EV_FX, pad, (int)fx, on ? 1.0f : 0.0f, 0.0f);
    return true;
}
