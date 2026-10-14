// ═══════════════════════════════════════════════════════════════════════════
//  app/app_effects.c — ◄ REGISTER NEW EFFECTS HERE
//
//  Every effect is LOCAL: each track has its own instance, sitting on that
//  track's channel (AT_HOOK_CHANNEL), and its hotkey switches it on the
//  selected track only. Crush on the piano never touches the drumpad, and an
//  echo tail belongs to the track that made it. (On the drumpad track the same
//  keys go one step further down: they switch the held pad's own effect, see
//  instruments/drumpad/drumpad_effects.c.)
//
//  An instance is made the first time the effect is switched on for a track,
//  so a track that never uses the echo never pays its ~1 MB.
//
//  To add an effect (after creating its folder under effects/):
//    1. #include its header
//    2. write a setup function that inits one instance with your settings
//    3. add() it in app_effects_setup: name, hotkey, slot, process, size, setup
//  That gives it a hotkey, a status line label and a "--fx name" option.
//  Effects run in slot order (AT_ENGINE_HOOK_SLOTS slots per track).
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include <stdlib.h>

#include "effects/bitcrusher/bitcrusher.h"
#include "effects/echo/echo.h"
#include "effects/tremolo/tremolo.h"

// ─── The sound of each effect ────────────────────────────────────────────────

static void crush_setup(void* state, float sample_rate) {
    (void)sample_rate;
    bitcrusher_init((AT_Bitcrusher*)state, 6, 5.0f);                        // hold every 6th sample, 5 bits
}

static void tremolo_setup(void* state, float sample_rate) {
    (void)sample_rate;
    tremolo_init((AT_Tremolo*)state, 6.0f, 0.8f);                           // 6 Hz, 80 % depth
}

static void echo_setup(void* state, float sample_rate) {
    echo_init((AT_Echo*)state, sample_rate, (float)AT_APP_ECHO_MS, 0.45f, 0.4f);   // 350 ms, each repeat 45 % of the last, 40 % level
}

// ─── Registry ────────────────────────────────────────────────────────────────

static void add(AT_App* app, const char* name, uint16_t hotkey, uint32_t slot,
                AT_ProcessFn process, size_t size, void (*setup)(void*, float)) {
    if (app->effect_count == AT_APP_MAX_EFFECTS || slot >= AT_ENGINE_HOOK_SLOTS) return;

    app->effects[app->effect_count++] = (AT_AppEffect){
        .name = name, .hotkey = hotkey, .slot = slot,
        .process = process, .set_param = app_effect_param_fn(process), .size = size, .setup = setup
    };
}

void app_effects_setup(AT_App* app) {
    app->effect_count = 0;
    for (int c = 0; c < AT_ENGINE_CHANNELS; c++) app->echo_ms[c] = AT_APP_ECHO_MS;

    //        name       hotkey    slot  process              size                    setup
    add(app, "crush",    AT_SC_C,  0,    bitcrusher_process,  sizeof(AT_Bitcrusher),  crush_setup);
    add(app, "tremolo",  AT_SC_V,  1,    tremolo_process,     sizeof(AT_Tremolo),     tremolo_setup);
    add(app, "echo",     AT_SC_F1, 2,    echo_process,        sizeof(AT_Echo),        echo_setup);
}

void app_effects_teardown(AT_App* app) {
    for (uint32_t i = 0; i < app->effect_count; i++)
        for (uint32_t c = 0; c < AT_ENGINE_CHANNELS; c++) {
            free(app->effects[i].state[c]);
            app->effects[i].state[c]   = NULL;
            app->effects[i].enabled[c] = false;
        }
}

int app_effect_find(const AT_App* app, const char* name) {
    for (uint32_t i = 0; i < app->effect_count; i++)
        if (!strcmp(app->effects[i].name, name)) return (int)i;
    return -1;
}

void* app_effect_state_channel(AT_App* app, uint32_t index, uint32_t channel) {
    if (index >= app->effect_count || channel >= AT_ENGINE_CHANNELS) return NULL;
    AT_AppEffect* fx = &app->effects[index];

    // First use on this channel: a fresh instance, set up before the engine sees it.
    if (!fx->state[channel]) {
        fx->state[channel] = calloc(1, fx->size);
        if (!fx->state[channel]) return NULL;
        fx->setup(fx->state[channel], app->sample_rate);
        if (fx->process == echo_process)                    // the channel's echo time, set before it plays
            echo_set_time_ms((AT_Echo*)fx->state[channel], app->echo_ms[channel]);
        // A recording that is running gets the new instance's settings, so its replay starts the
        // effect the same way, whatever an earlier pass left in the replay's own instance.
        app_record_effect(app, channel, index);
    }
    return fx->state[channel];
}

bool app_effect_set_channel(AT_App* app, uint32_t index, uint32_t channel, bool enabled) {
    if (index >= app->effect_count || channel >= AT_ENGINE_CHANNELS) return false;
    AT_AppEffect* fx = &app->effects[index];

    if (enabled && !app_effect_state_channel(app, index, channel)) return false;

    fx->enabled[channel] = enabled;
    engine_set_hook(&app->engine, AT_HOOK_CHANNEL, channel, fx->slot,
                    enabled ? fx->process : NULL, fx->state[channel]);
    return true;
}

bool app_effect_set(AT_App* app, uint32_t index, uint32_t track, bool enabled) {
    if (track >= app->tracks.count) return false;
    return app_effect_set_channel(app, index, app->tracks.tracks[track].channel, enabled);
}

int app_set_echo_ms_channel(AT_App* app, uint32_t channel, int ms) {
    if (channel >= AT_ENGINE_CHANNELS) return 0;
    int max = echo_max_ms_at(app->sample_rate);
    ms = ms < 1 ? 1 : ms > max ? max : ms;
    app->echo_ms[channel] = ms;

    int echo = app_effect_find(app, "echo");                // made already: it glides to the new time
    if (echo >= 0 && app->effects[echo].state[channel])
        engine_set_param(&app->engine, echo_set_param, app->effects[echo].state[channel], AT_ECHO_PARAM_TIME, (float)ms);
    return ms;
}

int app_set_echo_ms(AT_App* app, uint32_t track, int ms) {
    if (track >= app->tracks.count) return 0;
    ms = app_set_echo_ms_channel(app, app->tracks.tracks[track].channel, ms);

    // the drumpad's pads have their own echoes: the same time for all of them
    if (app->track_info[track].kind == AT_APP_KIND_DRUMPAD)
        for (int p = 0; p < AT_DRUMPAD_PADS; p++) drumpad_set_echo_ms((AT_Drumpad*)app->tracks.tracks[track].instrument.self, p, ms);
    return ms;
}

int app_echo_ms(const AT_App* app, uint32_t track) {
    return track < app->tracks.count ? app->echo_ms[app->tracks.tracks[track].channel] : 0;
}

bool app_effect_enabled(const AT_App* app, uint32_t index, uint32_t track) {
    if (index >= app->effect_count || track >= app->tracks.count) return false;
    return app->effects[index].enabled[app->tracks.tracks[track].channel];
}
