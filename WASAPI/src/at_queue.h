// ═══════════════════════════════════════════════════════════════════════════
//  at_queue.h — control → audio thread command ring
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
#include "at_common.h"

typedef struct {
    uint32_t type;
    int32_t  i[4];
    float    f[4];
    void*    p[2];
} AT_Command;

typedef struct {
    AT_Command       items[AT_QUEUE_CAPACITY];
    volatile LONG    head;              // next slot to write (producers)
    volatile LONG    tail;              // next slot to read  (consumer)
    CRITICAL_SECTION producer_lock;
} AT_Queue;

void at_queue_init   (AT_Queue* q);
void at_queue_destroy(AT_Queue* q);

// Control threads. Returns false if the ring is full (command dropped).
bool at_queue_push(AT_Queue* q, const AT_Command* cmd);

// Audio thread only. Returns false when empty. Never blocks.
bool at_queue_pop(AT_Queue* q, AT_Command* out);
