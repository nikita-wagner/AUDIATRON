#include "synth/synth_voices.h"

AT_SynthVoice* synth_pick_voice(AT_Synth* s, int note) {
    AT_SynthVoice* free_voice = NULL;
    AT_SynthVoice* steal      = NULL;

    for (int i = 0; i < AT_SYNTH_VOICES; i++) {
        AT_SynthVoice* v = &s->voices[i];
        // the same note still held: retriggered in place. Still fading out (released): a fresh voice instead, so
        // every note starts the same way (its oscillator from phase 0, its envelope from silence) however long
        // ago the last one was; the old one fades out on its own (or is the first to be stolen)
        if (v->active && v->note == note && v->env.stage != AT_ENV_RELEASE) return v;
        if (!v->active) { if (!free_voice) free_voice = v; continue; }

        if (!steal) { steal = v; continue; }
        bool v_rel = v->env.stage == AT_ENV_RELEASE;
        bool s_rel = steal->env.stage == AT_ENV_RELEASE;
        if (v_rel != s_rel) { if (v_rel) steal = v; }
        else if (v->age < steal->age) steal = v;
    }
    return free_voice ? free_voice : steal;
}
