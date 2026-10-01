#include "core/at_queue.h"

#include <string.h>

#define QUEUE_MASK (AT_QUEUE_CAPACITY - 1)

_Static_assert((AT_QUEUE_CAPACITY & QUEUE_MASK) == 0, "AT_QUEUE_CAPACITY must be a power of two");

void at_queue_init(AT_Queue* q) {
    memset(q, 0, sizeof *q);
    InitializeCriticalSection(&q->producer_lock);
}

void at_queue_destroy(AT_Queue* q) {
    DeleteCriticalSection(&q->producer_lock);
}

// head/tail only ever grow; unsigned subtraction keeps the fill level correct
// across the 2^32 wraparound.
bool at_queue_push(AT_Queue* q, const AT_Command* cmd) {
    EnterCriticalSection(&q->producer_lock);

    ULONG head = (ULONG)q->head;
    ULONG tail = (ULONG)ReadAcquire(&q->tail);
    bool  ok   = (head - tail) < AT_QUEUE_CAPACITY;
    if (ok) {
        q->items[head & QUEUE_MASK] = *cmd;
        WriteRelease(&q->head, (LONG)(head + 1));   // publish after the item is written
    }

    LeaveCriticalSection(&q->producer_lock);
    return ok;
}

bool at_queue_pop(AT_Queue* q, AT_Command* out) {
    ULONG tail = (ULONG)q->tail;
    ULONG head = (ULONG)ReadAcquire(&q->head);
    if (tail == head) return false;

    *out = q->items[tail & QUEUE_MASK];
    WriteRelease(&q->tail, (LONG)(tail + 1));       // free the slot after the copy
    return true;
}
