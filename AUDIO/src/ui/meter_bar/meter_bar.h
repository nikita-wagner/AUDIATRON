// ═══════════════════════════════════════════════════════════════════════════
//  ui/meter_bar/meter_bar.h — a level meter drawn with text characters
//
//      [############........]     -60 dB .. 0 dB, the last two cells show '!'
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// Writes `width` characters plus a terminating 0 to out (needs width + 1 bytes).
// peak is linear: 1.0 = full scale.
void meter_bar_format(char* out, int width, float peak);
