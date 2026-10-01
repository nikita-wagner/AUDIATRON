// ═══════════════════════════════════════════════════════════════════════════
//  app/app_effects.c — ◄ REGISTER NEW EFFECTS HERE
//
//  To add an effect (after creating its folder under effects/):
//    1. #include its header
//    2. add a static instance
//    3. init it and add() it in app_effects_setup: name, hotkey, where in the
//       signal graph (hook point, channel, slot), its process function
//  That gives it a hotkey, a status line label and a "--fx name" option.
//
//  Two effects on the same hook point and channel need different slots; they
//  run in slot order.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"

#include "effects/bitcrusher/bitcrusher.h"
#include "effects/tremolo/tremolo.h"

// The app is the composition root, so it owns the instances.
static AT_Bitcrusher s_crusher;
static AT_Tremolo    s_tremolo;

static void add(AT_App* app, const char* name, uint16_t hotkey,
                AT_HookPoint point, uint32_t channel, uint32_t slot,
                AT_ProcessFn process, void* state) {
    if (app->effect_count == AT_APP_MAX_EFFECTS) return;

    app->effects[app->effect_count++] = (AT_AppEffect){
        .name = name, .hotkey = hotkey,
        .point = point, .channel = channel, .slot = slot,
        .process = process, .state = state
    };
}

void app_effects_setup(AT_App* app, float sample_rate) {
    (void)sample_rate;      // for effects whose init needs it (delay lines, filters...)
    app->effect_count = 0;

    bitcrusher_init(&s_crusher, 6, 5.0f);       // hold every 6th sample, 5 bits
    tremolo_init(&s_tremolo, 6.0f, 0.8f);       // 6 Hz, 80 % depth

    //        name       hotkey    hook point            ch slot  process             state
    add(app, "crush",    AT_SC_C,  AT_HOOK_MASTER_POST,  0, 0,    bitcrusher_process, &s_crusher);
    add(app, "tremolo",  AT_SC_V,  AT_HOOK_CHANNEL,      0, 0,    tremolo_process,    &s_tremolo);
}

int app_effect_find(const AT_App* app, const char* name) {
    for (uint32_t i = 0; i < app->effect_count; i++)
        if (!strcmp(app->effects[i].name, name)) return (int)i;
    return -1;
}

void app_effect_set(AT_App* app, uint32_t index, bool enabled) {
    if (index >= app->effect_count) return;

    AT_AppEffect* fx = &app->effects[index];
    fx->enabled = enabled;
    engine_set_hook(&app->engine, fx->point, fx->channel, fx->slot,
                    enabled ? fx->process : NULL, fx->state);
}
