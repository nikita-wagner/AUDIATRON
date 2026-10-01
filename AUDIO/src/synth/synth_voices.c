#include "synth/synth_voices.h"

AT_SynthVoice* synth_pick_voice(AT_Synth* s, int note) {
    AT_SynthVoice* free_voice = NULL;
    AT_SynthVoice* steal      = NULL;

    for (int i = 0; i < AT_SYNTH_VOICES; i++) {
        AT_SynthVoice* v = &s->voices[i];
        if (v->active && v->note == note) return v;
        if (!v->active) { if (!free_voice) free_voice = v; continue; }

        if (!steal) { steal = v; continue; }
        bool v_rel = v->env.stage == AT_ENV_RELEASE;
        bool s_rel = steal->env.stage == AT_ENV_RELEASE;
        if (v_rel != s_rel) { if (v_rel) steal = v; }
        else if (v->age < steal->age) steal = v;
    }
    return free_voice ? free_voice : steal;
}
