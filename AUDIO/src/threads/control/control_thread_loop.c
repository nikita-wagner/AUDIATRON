// ═══════════════════════════════════════════════════════════════════════════
//  threads/control/control_thread_loop.c — the control loop
//
//  One iteration: wait for keys (at most tick_ms) → deliver each key event →
//  tick. The wait is what paces the loop; there is no Sleep.
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/control/control_thread.h"

#define MAX_EVENTS 32

void control_thread_loop(AT_ControlThread* t) {
    const AT_ControlThreadConfig* cfg = &t->config;

    for (;;) {
        AT_KeyEvent events[MAX_EVENTS];
        uint32_t    count = console_keys_poll(&t->keys, cfg->tick_ms, events, MAX_EVENTS);

        for (uint32_t i = 0; i < count; i++)
            if (cfg->on_key && !cfg->on_key(cfg->user, &events[i])) return;

        if (cfg->on_tick && !cfg->on_tick(cfg->user)) return;
    }
}
