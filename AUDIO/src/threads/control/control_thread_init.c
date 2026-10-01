// ═══════════════════════════════════════════════════════════════════════════
//  threads/control/control_thread_init.c — setup / teardown
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/control/control_thread.h"

bool control_thread_init(AT_ControlThread* t, const AT_ControlThreadConfig* config) {
    t->config = *config;
    if (t->config.tick_ms == 0) t->config.tick_ms = 33;     // ~30 ticks per second

    return console_keys_open(&t->keys);
}

void control_thread_shutdown(AT_ControlThread* t) {
    console_keys_close(&t->keys);
}
