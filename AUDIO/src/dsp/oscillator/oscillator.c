#include "dsp/oscillator/oscillator.h"

const char* oscillator_waveform_name(AT_Waveform w) {
    switch (w) {
    case AT_WAVE_SINE:     return "sine";
    case AT_WAVE_SAW:      return "saw";
    case AT_WAVE_SQUARE:   return "square";
    case AT_WAVE_TRIANGLE: return "triangle";
    case AT_WAVE_CIRCLE:   return "circle";
    default:               return "?";
    }
}

void oscillator_init(AT_Oscillator* o, uint32_t noise_seed) {
    o->phase       = 0.0;
    o->phase_inc   = 0.0;
    o->noise_state = noise_seed ? noise_seed : 0x9E3779B9u;    // xorshift must never be 0
}
