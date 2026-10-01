// ═══════════════════════════════════════════════════════════════════════════
//  instruments/_template/template_instrument.h — COPY THIS FOLDER to start an
//  instrument (drumpad, guitar, ...)
//
//  1. Copy instruments/_template/ to instruments/<your_instrument>/ and rename
//     both files (file names must be unique in the whole project).
//  2. Rename AT_TemplateInstrument / template_instrument_* to your name.
//  3. Decide the SOUND: keep the AT_Synth, or put your own source struct here
//     (anything with an AT_SourceType — see synth/synth_source.c).
//  4. Decide the PLAYING: fill in the key table in the .c file.
//  5. Register it in app/app_instruments.c (two lines). It gets its own track
//     (its own mixer channel); Tab selects the track the keyboard plays.
//
//  As it stands the template is a working 4-pad instrument on keys A S D F.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include "instruments/instrument.h"
#include "synth/synth.h"

typedef struct {
    AT_Synth   synth;                   // the sound source — swap for your own
    AT_Engine* engine;                  // set by attach
    uint32_t   channel;
    bool       held[AT_SC_COUNT];       // which keys currently hold a note
} AT_TemplateInstrument;

void          template_instrument_init      (AT_TemplateInstrument* t, float sample_rate);
AT_Instrument template_instrument_instrument(AT_TemplateInstrument* t);
