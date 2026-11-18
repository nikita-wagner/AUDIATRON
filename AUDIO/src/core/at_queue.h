// ═══════════════════════════════════════════════════════════════════════════
//  core/at_queue.h — control → audio thread command ring
//
//  The WinMM engine protected every sound with a CRITICAL_SECTION that the
//  mixer also held while mixing. That works, but the audio thread can then be
//  blocked by the UI thread — the classic source of dropouts.
//
//  Here the audio thread NEVER waits: control threads push small fixed-size
//  commands into a ring, the audio thread drains it at the start of each block
//  and applies the changes to state only it touches.
//
//  Producers (any number of control threads) serialize on a lock among
//  themselves; the single consumer (audio thread) is wait-free.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

#define AT_QUEUE_CAPACITY 2048      // pending commands (must be a power of two); a replay sends many at once

typedef struct {
    uint32_t type;
    int32_t  i[4];
    float    f[4];
    void*    p[2];
    uint64_t frame;                 // when to apply it (engine frame); 0 = as soon as possible
    int32_t  tag;                   // groups timed commands so they can be cancelled together; 0 = none
} AT_Command;

typedef struct {
    AT_Command       items[AT_QUEUE_CAPACITY];
    volatile LONG    head;              // next slot to write (producers)
    volatile LONG    tail;              // next slot to read  (consumer)
    CRITICAL_SECTION producer_lock;
    volatile LONG    dropped;           // commands that found the ring full, ever
} AT_Queue;

void at_queue_init   (AT_Queue* q);
void at_queue_destroy(AT_Queue* q);

// Control threads. Returns false if the ring is full (command dropped and counted).
bool at_queue_push(AT_Queue* q, const AT_Command* cmd);

// Any thread: how many commands were dropped so far.
long at_queue_dropped(const AT_Queue* q);

// Audio thread only. Returns false when empty. Never blocks.
bool at_queue_pop(AT_Queue* q, AT_Command* out);
