// ═══════════════════════════════════════════════════════════════════════════
//  threads/audio/audio_thread_priority.c — real-time priority for the thread
//
//  Windows Vista and later: MMCSS, the scheduler class for pro audio. Much
//  stronger than a thread priority — this is what DAWs use.
//
//  MMCSS lives in avrt.dll, which older Windows versions do not have. Linking
//  against it would stop the whole program from starting there, so it is
//  looked up at run time instead. Where it is missing (95 / 98 / Me / 2000 /
//  XP) the thread gets the highest ordinary priority.
// ═══════════════════════════════════════════════════════════════════════════

#include "threads/audio/audio_thread.h"

// The three avrt.dll functions we use, declared here so the build needs
// neither avrt.h nor avrt.lib.
typedef HANDLE (WINAPI *AvSetCharacteristicsFn)(const char* task_name, DWORD* task_index);
typedef BOOL   (WINAPI *AvSetPriorityFn)(HANDLE task, int priority);
typedef BOOL   (WINAPI *AvRevertFn)(HANDLE task);

#define AVRT_PRIORITY_HIGH_VALUE 1          // AVRT_PRIORITY_HIGH in avrt.h

typedef struct {
    HMODULE    avrt;                        // NULL = MMCSS not in use
    HANDLE     task;
    AvRevertFn revert;
} PriorityState;

// One audio thread per process raises its priority, so one state is enough.
static PriorityState s_state;

void* audio_thread_priority_enter(void) {
    PriorityState* state = &s_state;
    state->avrt = LoadLibraryA("avrt.dll");

    if (state->avrt) {
        AvSetCharacteristicsFn set_characteristics =
            (AvSetCharacteristicsFn)(void*)GetProcAddress(state->avrt, "AvSetMmThreadCharacteristicsA");
        AvSetPriorityFn set_priority =
            (AvSetPriorityFn)(void*)GetProcAddress(state->avrt, "AvSetMmThreadPriority");
        state->revert = (AvRevertFn)(void*)GetProcAddress(state->avrt, "AvRevertMmThreadCharacteristics");

        DWORD task_index = 0;
        state->task = (set_characteristics && state->revert) ? set_characteristics("Pro Audio", &task_index) : NULL;
        if (state->task) {
            if (set_priority) set_priority(state->task, AVRT_PRIORITY_HIGH_VALUE);
            return state;
        }

        FreeLibrary(state->avrt);           // present but refused: fall through
        state->avrt = NULL;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return NULL;
}

void audio_thread_priority_leave(void* token) {
    PriorityState* state = (PriorityState*)token;
    if (!state || !state->avrt) return;

    state->revert(state->task);
    FreeLibrary(state->avrt);
    state->avrt = NULL;
    state->task = NULL;
}
