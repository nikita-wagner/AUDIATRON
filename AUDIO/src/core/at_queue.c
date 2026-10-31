#include "core/at_queue.h"

#include <string.h>

#define QUEUE_MASK (AT_QUEUE_CAPACITY - 1)

// Publishing an index to the other thread. Recent Microsoft headers have
// ReadAcquire / WriteRelease for exactly this. Other compilers and older SDKs
// get the Interlocked functions, which every Windows since 98 has.
#if defined(_MSC_VER) && _MSC_VER >= 1900 && !defined(AT_QUEUE_PORTABLE_ATOMICS)
#define LOAD_INDEX(p)       ReadAcquire(p)
#define STORE_INDEX(p, v)   WriteRelease((p), (v))
#else
#define LOAD_INDEX(p)       InterlockedCompareExchange((p), 0, 0)     // changes nothing, returns the value
#define STORE_INDEX(p, v)   InterlockedExchange((p), (v))
#endif

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
    ULONG tail = (ULONG)LOAD_INDEX(&q->tail);
    bool  ok   = (head - tail) < AT_QUEUE_CAPACITY;
    if (ok) {
        q->items[head & QUEUE_MASK] = *cmd;
        STORE_INDEX(&q->head, (LONG)(head + 1));    // publish after the item is written
    } else {
        InterlockedIncrement(&q->dropped);
    }

    LeaveCriticalSection(&q->producer_lock);
    return ok;
}

long at_queue_dropped(const AT_Queue* q) {
    return InterlockedCompareExchange((volatile LONG*)&q->dropped, 0, 0);
}

bool at_queue_pop(AT_Queue* q, AT_Command* out) {
    ULONG tail = (ULONG)q->tail;
    ULONG head = (ULONG)LOAD_INDEX(&q->head);
    if (tail == head) return false;

    *out = q->items[tail & QUEUE_MASK];
    STORE_INDEX(&q->tail, (LONG)(tail + 1));        // free the slot after the copy
    return true;
}
