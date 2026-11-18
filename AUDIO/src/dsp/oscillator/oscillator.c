#include "dsp/oscillator/oscillator.h"

float at_sine_table[AT_SINE_SIZE + 1];

const char* oscillator_waveform_name(AT_Waveform w) {
    switch (w) {
    case AT_WAVE_SINE:     return "sine";
    case AT_WAVE_SAW:      return "saw";
    case AT_WAVE_SQUARE:   return "square";
    case AT_WAVE_TRIANGLE: return "triangle";
    case AT_WAVE_CIRCLE:   return "circle";
    case AT_WAVE_PLUCK:    return "pluck";
    default:               return "?";
    }
}

void oscillator_init(AT_Oscillator* o, uint32_t noise_seed) {
    o->phase       = 0.0;
    o->phase_inc   = 0.0;
    o->noise_state = noise_seed ? noise_seed : 0x9E3779B9u;    // xorshift must never be 0

    if (at_sine_table[AT_SINE_SIZE / 4] == 0.0f) {             // the peak of the cycle: 0 until it is filled
        for (int i = 0; i < AT_SINE_SIZE; i++)
            at_sine_table[i] = (float)sin(AT_TWO_PI * i / AT_SINE_SIZE);
        at_sine_table[AT_SINE_SIZE] = at_sine_table[0];
    }
}
