// ═══════════════════════════════════════════════════════════════════════════
//  synth/synth_voices.h — voice allocation (private to the synth folder)
//
//  Fixes the WinMM "same note stacks and beats" problem at the source instead
//  of with phase lock:
//    1. note already sounding → that voice is retriggered, never duplicated
//    2. otherwise a free voice
//    3. otherwise steal: releasing voices first, then the oldest
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "synth/synth.h"

// Never returns NULL.
AT_SynthVoice* synth_pick_voice(AT_Synth* s, int note);
