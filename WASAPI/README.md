# AUDIATRON: Raw-C WASAPI Audio Engine

Nachfolger der WinMM-Engine (`../WINMM/winmm.h`). Die Engine ist in reinem C11 ohne C++ und ohne Libraries geschrieben. Das Ziel ist volle Kontrolle über jeden Sample-Buffer, bevor er das Gerät erreicht.

```
build.bat                         → build\audiatron.exe  (build.bat debug für Debug-Build)
audiatron                         Tastatur-Piano, Shared Mode, Standardgerät
audiatron --exclusive             Exclusive Mode (bei dir: 3 ms statt ~150 ms WinMM)
audiatron --list                  Geräte auflisten
audiatron --device 2 --exclusive  bestimmtes Gerät
audiatron --info [--exclusive]    nur Format aushandeln und ausgeben
audiatron --offline out.wav 5     ohne Gerät in eine WAV rendern (zum Testen von DSP/Hooks)
audiatron --crush --tremolo       mit Demo-Hooks starten
```

---

## 1. Signalfluss

```
 Control-Thread (main.c, UI, MIDI...)
        │  engine_note_on(), engine_set_eq_band(), engine_set_hook() ...
        ▼
 ┌──────────────────┐   lock-free, Audio-Thread wartet nie
 │  AT_Queue (Ring) │
 └────────┬─────────┘
          ▼  am Anfang jedes Blocks abgearbeitet
 ════════ Audio-Thread (MMCSS "Pro Audio", event-driven) ══════════════════════
  pro Mixer-Kanal (8x):
    Synth / eigene Source ─[H] SOURCE─▶ Kanal-EQ ─[H] CHANNEL─▶ Fader+Pan ─┐
                                                                            ▼
  Master-Bus:  Σ ─[H] MASTER_PRE─▶ Master-EQ ─▶ Master-Fader ─[H] MASTER_POST─▶
               Soft-Clip-Limiter ─[H] OUTPUT─▶ float→Geräteformat ─▶ WASAPI
 ═════════════════════════════════════════════════════════════════════════════
  [H] = Hook-Punkt: dein Code bekommt den rohen float-Buffer (planar, in place),
        4 verkettete Slots pro Punkt, mit frame_clock für sample-genaues Timing.
```

---

## 2. Datei-Sheet

| # | Datei | Schicht | Verantwortung | Läuft auf | Status |
|---|---|---|---|---|---|
| 1 | `src/at_common.h` | Basis | Konstanten, `AT_Buffer` (planar float), Helfer (dB, Note→Hz, Flush-to-Zero) | beide | ✅ fertig |
| 2 | `src/at_queue.h/.c` | Basis | Command-Ring Control → Audio; Producer mit Lock, Consumer wait-free | beide | ✅ fertig |
| 3 | `src/wasapi_backend.h/.c` | Plattform | COM, Geräteliste, Format-Aushandlung Shared/Exclusive, Event-Thread, float→int16/24/32 | Audio + Control | ✅ fertig |
| 4 | `src/dsp_engine.h/.c` | DSP-Kern | Signalgraph, Hook-System, Command-Anwendung, Block-Splitting, Control-API | Audio (+ API: Control) | ✅ fertig |
| 5 | `src/synth.h/.c` | DSP | 32-stimmig, Sine/Saw/Square (PolyBLEP)/Tri/Noise, lineare ADSR, Voice-Stealing | Audio | ✅ Basis |
| 6 | `src/equalizer.h/.c` | DSP | RBJ-Biquads (LP/HP/BP/Notch/Peak/Shelves), 8 Bänder, TDF-II | Audio | ✅ Basis |
| 7 | `src/mixer.h/.c` | DSP | 8 Kanäle: Volume/Pan/Mute + Smoothing, Master, Soft-Clip, Peak-Meter | Audio | ✅ Basis |
| 8 | `src/main.c` | App | CLI, Tastatur-Piano (Scancodes → QWERTZ-sicher), Meter, 2 Demo-Hooks, Offline-WAV | Control | ✅ Demo |
| 9 | `build.bat` | Build | MSVC via vswhere, `/W4` warnungsfrei, links `ole32` + `avrt` | – | ✅ |
| – | `src/sampler.h/.c` | DSP | WAV laden + abspielen als `AT_SourceFn` (Port aus winmm.h) | Audio | ⬜ Phase 5 |
| – | `src/effects.h/.c` | DSP | Delay, Reverb (Schroeder/FDN), Chorus, Kompressor | Audio | ⬜ Phase 6 |
| – | `src/midi_in.h/.c` | Control | `midiInOpen` → `engine_note_on` | Control | ⬜ Phase 7 |
| – | `src/device_watch.h/.c` | Plattform | `IMMNotificationClient`: Hot-Plug, Standardgerät gewechselt | Control | ⬜ Phase 7 |
| – | `src/capture.h/.c` | Plattform | `IAudioCaptureClient`: Mikro/Line-In als Source | Audio | ⬜ Phase 8 |

---

## 3. Roadmap

**Phase 0: Verstehen** (lesen, siehe §6)
- [ ] C-COM-Muster: `obj->lpVtbl->Method(obj, ...)`, `Release()`, HRESULTs
- [ ] `wasapi_backend.c` von oben nach unten lesen, er ist bewusst ohne `COBJMACROS` geschrieben

**Phase 1: Backend** ✅
- [x] Geräte auflisten (`--list`), Friendly Name über `IPropertyStore`
- [x] Shared Mode event-driven, float32 mit `AUTOCONVERTPCM`
- [x] Exclusive Mode: Format-Probing, Minimum-Periode, `BUFFER_SIZE_NOT_ALIGNED`-Retry
- [x] MMCSS "Pro Audio", Flush-to-Zero, Overrun-Zähler, Device-Lost-Flag

**Phase 2: Engine-Gerüst** ✅
- [x] Planar-float-Buffer, lock-freie Command-Queue, 5 Hook-Punkte × 4 Slots
- [x] Offline-Rendering in WAV zum Testen ohne Gerät

**Phase 3: Synth ausbauen**
- [ ] ADSR exponentiell statt linear (klingt natürlicher)
- [ ] 2 Oszillatoren pro Stimme + Detune, Unison
- [ ] Filter pro Stimme (State-Variable-Filter oder Moog-Ladder) + Filter-Hüllkurve
- [ ] LFOs mit Modulationsmatrix (Ziel: Pitch, Cutoff, Amp, Pan)
- [ ] Pitch-Bend, Velocity-Kurven, Portamento

**Phase 4: EQ und Mixer ausbauen**
- [ ] Koeffizienten-Smoothing (Sweeps ohne Knacken)
- [ ] Frequenzgang-Berechnung aus `eq_design()` für eine grafische EQ-Anzeige
- [ ] Send/Return-Busse (Reverb-Bus), Solo, Gruppen
- [ ] Echter Look-ahead-Limiter statt Soft-Clip

**Phase 5: Sampler** (Port von `winmm.h`)
- [ ] WAV-Loader (8/16/24/32-bit, SIMD-Konvertierung aus winmm.h übernehmen) → float
- [ ] Sampler als `AT_SourceFn`: Loop-Punkte, Pitch, Resampling (linear → kubisch/Hermite)

**Phase 6: Effekte** (als Hooks oder feste Kette)
- [ ] Delay (Ping-Pong), Reverb, Chorus/Flanger, Kompressor, Distortion/Waveshaper

**Phase 7: Robustheit**
- [ ] `IMMNotificationClient`: Gerät abgezogen → automatisch neu öffnen
- [ ] Sample-genaue Events (Frame-Timestamp in `AT_Command`, Block am Event splitten)
- [ ] MIDI-Input

**Phase 8: Extras**
- [ ] Capture (Line-In/Mikro durch die Kette), Recording, FFT-Analyzer-Hook, GUI

---

## 4. WinMM → WASAPI: was wurde aus deinen Funktionen?

| winmm.h | Neue Engine | Warum besser |
|---|---|---|
| `audio_init()` + `audio_pump()` alle 3 ms | `backend_open()` + `backend_start()` | Das Gerät weckt den Thread per Event, kein Polling mehr. MMCSS-Priorität |
| 3 × 2205 Frames ≈ 150 ms Latenz | 10 ms (Shared) / **3 ms** (Exclusive) | Direkter Hardware-Buffer |
| `short` interleaved, Clamp in jedem `mix_stereo` | `float` planar, Konvertierung nur einmal am Ende | Kein Zwischen-Clipping, Headroom für EQ |
| `CRITICAL_SECTION` im Mixer | `AT_Queue` (lock-free) | Die UI kann den Audio-Thread nie blockieren |
| `play_tone(freq, gain)` | `engine_note_on(e, ch, note, vel)` | Stimmenverwaltung, ADSR, kein Phase-Beating (Retrigger statt Stapeln) |
| Phase-Lock über `g_mix_clock` | `AT_Buffer.frame_clock` in jedem Hook | Absolute Sample-Zeit überall verfügbar |
| `angle_of_tone()` | `engine_set_channel_pan()` | Pro Kanal, geglättet |
| `reverb_tone()` (ein Delay) | Hook-Punkt / Phase 6 | Beliebige eigene Effekte |
| `set_amp_tone()` | `engine_set_channel_volume()` / Velocity | Geglättet, ohne Zipper |
| `play_*_WAV()` | Phase 5: Sampler als `AT_SourceFn` | – |
| SIMD-Konverter 8/24/32→16 | wiederverwenden im WAV-Loader (Ziel: float) | – |

---

## 5. Die Regeln für den Audio-Thread

Code, der in `engine_render`, in einem Hook oder in einer Source läuft:

1. **Kein** `malloc`/`free`, **kein** `printf`, **keine** Datei-I/O, **keine** Locks, kein `Sleep`. Alles davon kann Millisekunden blockieren, und das hörst du als Knacksen.
2. Speicher vorher allokieren. Hook-Zustand gehört in den `user`-Pointer, und dieser Speicher muss länger leben als die Registrierung.
3. Parameter immer über `engine_*()` ändern, nie direkt in Engine-Structs schreiben.
4. Jede COM-Schnittstelle genau einmal `Release()`n. In `wasapi_backend.c` erledigt das `goto fail`.
5. Zu den Tipps aus deinem Plan zwei Korrekturen:
   - Der **Exclusive Mode** ist *nicht* an `GetMixFormat` oder die Einstellung in der Systemsteuerung gebunden. Das Mix-Format gilt für den Shared Mode. Im Exclusive Mode probt man mit `IsFormatSupported(EXCLUSIVE, …)`, und alles, was die Hardware kann, ist erlaubt (siehe `init_exclusive`). Bei deinem Razer ist das nur int16.
   - Statt `SetThreadPriority` nimmt man **MMCSS** (`AvSetMmThreadCharacteristicsW(L"Pro Audio")`). Das nutzen auch DAWs.

---

## 6. Leseliste (in dieser Reihenfolge)

**Microsoft Learn** (Suchbegriffe)
1. *About WASAPI*
2. *Stream Management*: Lebenszyklus `Initialize → Start → GetBuffer/ReleaseBuffer → Stop`
3. *Rendering a Stream*: die Grundschleife (entspricht `audio_thread_proc`)
4. *Exclusive-Mode Streams*: Event-Modus, Perioden, der `BUFFER_SIZE_NOT_ALIGNED`-Tanz
5. *IAudioClient::Initialize* (Referenz): alle Flags und Fehlercodes
6. *Multimedia Class Scheduler Service*

**Windows-SDK-Header** (`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\`)
- `audioclient.h`, `mmdeviceapi.h`: nach `IAudioClientVtbl` suchen, dort stehen die exakten C-Signaturen

**Open Source**
- **miniaudio.h**: nach `ma_device_init__wasapi` und `AUDCLNT_SHAREMODE_EXCLUSIVE` suchen. Bestes reines C-WASAPI, das es gibt
- **PortAudio** `src/hostapi/wasapi/pa_win_wasapi.c`: Puffergrößen-Berechnung, viele Hardware-Workarounds

**DSP**
- Robert Bristow-Johnson: *Audio EQ Cookbook*. Daraus stammt `equalizer.c` 1:1
- Välimäki et al.: *PolyBLEP*. Band-limitierte Oszillatoren (`synth.c`)
- Will Pirkle: *Designing Software Synthesizer Plug-Ins in C++* (Konzepte 1:1 auf C übertragbar)
- musicdsp.org: Filter, Oszillatoren, Effekte als Snippets
- Julius O. Smith: *Introduction to Digital Filters* (online, kostenlos), zum Verstehen, *warum* Biquads funktionieren
