// ═══════════════════════════════════════════════════════════════════════════
//  threads/control/control_thread.h — the non-real-time thread (main thread)
//
//  Reads the keyboard and ticks the UI. It knows nothing about the app, the
//  engine or instruments: it only delivers key events and a periodic tick to
//  two callbacks. Whoever sets it up decides what they do (see app/).
//
//  Files in this folder:
//    control_thread_init.c   setup / teardown (console into raw key mode)
//    control_thread_loop.c   the loop: poll keys → on_key, then on_tick
//
//  Unlike the audio thread, this one may block, allocate, print and do file
//  I/O. It talks to the audio thread only through engine_* commands.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "input/console_keys/console_keys.h"

typedef struct {
    bool (*on_key) (void* user, const AT_KeyEvent* ev);     // return false to end the loop
    bool (*on_tick)(void* user);                            // return false to end the loop
    void*    user;
    uint32_t tick_ms;                                       // longest time between two ticks
} AT_ControlThreadConfig;

typedef struct {
    AT_ControlThreadConfig config;
    AT_ConsoleKeys         keys;
} AT_ControlThread;

// ─── control_thread_init.c ───────────────────────────────────────────────────

// false = no interactive console available.
bool control_thread_init    (AT_ControlThread* t, const AT_ControlThreadConfig* config);
void control_thread_shutdown(AT_ControlThread* t);

// ─── control_thread_loop.c ───────────────────────────────────────────────────

// Runs on the calling thread until a callback returns false.
void control_thread_loop(AT_ControlThread* t);
