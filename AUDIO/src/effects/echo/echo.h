// ═══════════════════════════════════════════════════════════════════════════
//  effects/echo/echo.h — echo / delay with feedback
//
//  Repeats the sound after `time` seconds, then again, each repeat quieter by
//  `feedback`:
//
//      in ──┬──────────────────────────────▶ (+) ──▶ out
//           │                                 ▲
//           └─▶ [ delay line ] ──▶ mix ───────┘
//                    ▲        │
//                    └ feedback ◀─ damping ◀─┘
//
//    time_ms    delay between repeats (clamped to AT_ECHO_MAX_SECONDS at 48 kHz)
//    feedback   0 = a single repeat, 0.5 = each repeat half as loud, max 0.95
//    mix        level of the echoes added to the original, 0 = effect off
//    damping    0 = bright repeats, 1 = each repeat darker than the one before
//    ping_pong  on: the repeats bounce between left and right
//
//  Changing the time glides to the new value (like a tape echo: the repeats
//  bend in pitch for a moment) instead of clicking.
//
//  The tail keeps ringing after the input goes silent, because the hook runs
//  every block.
//
//      static AT_Echo echo;
//      echo_init(&echo, sample_rate, 350.0f, 0.45f, 0.4f);
//      engine_set_hook(&engine, AT_HOOK_CHANNEL, channel, 1, echo_process, &echo);
//      engine_set_param(&engine, echo_set_param, &echo, AT_ECHO_PARAM_TIME, 500.0f);
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "core/at_buffer.h"

#define AT_ECHO_CAPACITY      131072        // frames per channel (2.7 s at 48 kHz, 5.9 s at 22 kHz)
#define AT_ECHO_MAX_FEEDBACK  0.95f

typedef struct {
    float    sample_rate;
    float    time_ms;                       // target
    float    feedback;
    float    mix;
    float    damping;
    bool     ping_pong;

    // ── state ──
    double   delay_frames;                  // the delay in use now, gliding towards time_ms (double: a float
                                            // gliding by tiny steps would stall a few frames short)
    double   glide_coef;
    float    damp_state[AT_MAX_CHANNELS];   // one-pole lowpass memory inside the feedback path
    uint32_t write;
    float    line[AT_MAX_CHANNELS][AT_ECHO_CAPACITY];
} AT_Echo;

typedef enum {
    AT_ECHO_PARAM_TIME,                     // milliseconds
    AT_ECHO_PARAM_FEEDBACK,                 // 0 .. 0.95
    AT_ECHO_PARAM_MIX,                      // 0 .. 1
    AT_ECHO_PARAM_DAMPING,                  // 0 .. 1
    AT_ECHO_PARAM_PING_PONG                 // 0 / 1
} AT_EchoParam;

// ~1 MB: allocate statically or on the heap, not on a thread stack.
void echo_init(AT_Echo* e, float sample_rate, float time_ms, float feedback, float mix);

void echo_process  (AT_Buffer* buf, void* user);                 // AT_ProcessFn, user = AT_Echo*
void echo_set_param(void* self, int param, float value);         // AT_ParamFn
void echo_clear    (AT_Echo* e);                                 // forget the tail (audio thread)

// ─── The time between repeats, in whole milliseconds ─────────────────────────
//
// Directly (before the echo is plugged in, or on the audio thread):
//     echo_set_time_ms(&echo, 120);
// While it runs, from the control thread (glides there in ~0.15 s, no click):
//     engine_set_param(&engine, echo_set_param, &echo, AT_ECHO_PARAM_TIME, 120.0f);
// Clamped to 1 .. echo_max_ms(). For a game: the echo of a wall `meters` away
// comes back after echo_ms_for_distance(meters), sound going there and back.
void echo_set_time_ms(AT_Echo* e, int ms);
int  echo_time_ms    (const AT_Echo* e);
int  echo_max_ms     (const AT_Echo* e);                         // the delay line's length (2730 at 48 kHz)
int  echo_max_ms_at  (float sample_rate);                        // the same without an echo at hand

#define AT_SPEED_OF_SOUND  343.0f                                // m/s in air at 20 °C

static inline int echo_ms_for_distance(float meters) {
    float ms = 2.0f * meters / AT_SPEED_OF_SOUND * 1000.0f;      // there and back
    return ms < 1.0f ? 1 : (int)(ms + 0.5f);
}
