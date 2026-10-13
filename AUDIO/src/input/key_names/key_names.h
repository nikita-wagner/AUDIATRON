// ═══════════════════════════════════════════════════════════════════════════
//  input/key_names/key_names.h — the label printed on a physical key
//
//  Scan codes name positions; this asks Windows what is printed on that key
//  in the user's current keyboard layout (AT_SC_Z → "Y" on a German keyboard).
//  For help texts — never for deciding what a key does.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Writes e.g. "C", "Tab", "Esc" to out; "?" if the key has no name.
void key_name(uint16_t scan_code, char* out, size_t cap);

// The same for an extended key ("Up" instead of the numpad's "8").
void key_name_ex(uint16_t scan_code, bool extended, char* out, size_t cap);
