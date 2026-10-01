# AUDIATRON: raw-C WASAPI audio engine

Successor of the WinMM engine (`../WINMM/winmm.h`). Plain C11, no C++, no libraries. Built so that every part is its own folder, can be reused on its own, and new parts can be added without touching existing ones.

```
build.bat                           → build\audiatron.exe      (build.bat debug for a debug build)
audiatron                           live: keyboard instruments, shared mode, default device
audiatron --exclusive               exclusive mode (3 ms period on the Razer headset)
audiatron --list                    list output devices
audiatron --device 2 --exclusive    pick a device
audiatron --info [--exclusive]      only negotiate the format and print it
audiatron --fx crush --fx tremolo   start with effects switched on
audiatron --track 2                 start on track 2 (piano + strings + synth layered)
audiatron --offline out.wav 5       render a demo into a WAV file, no device needed
audiatron --offline out.wav 5 --track 2      the same demo played on track 2
```

---

## 1. Folder map

One folder = one module. Every `.c` file under `src\` is compiled automatically.

```
src/
├── main.c                  entry point: parse arguments, pick a mode. No logic.
│
├── core/                   FOUNDATION: depends on nothing
│   ├── at_config.h           the two limits everything shares
│   ├── at_buffer.h           AT_Buffer (planar float) + AT_ProcessFn, the one signature
│   ├── at_math.h             clamp, dB, note→Hz, smoothing, flush-to-zero
│   ├── at_module.h           the contracts: AT_SourceType, AT_ParamFn
│   └── at_queue.h/.c         lock-free command ring, control thread → audio thread
│
├── dsp/                    BUILDING BLOCKS: depend on core only
│   ├── oscillator/           one band-limited oscillator, 5 waveforms
│   ├── envelope/             ADSR
│   └── biquad/               one filter stage (RBJ cookbook)
│
├── synth/                  polyphonic synth = oscillator × ADSR × 32 voices
├── layers/                 LAYERS: several sources played together as ONE sound
│   ├── layer_stack.c         per layer: source → inserts → volume/pan, all mixed
│   └── layer_stack_source.c  lets the engine use a stack as a source
├── equalizer/              8-band parametric EQ = chain of biquads
├── mixer/
│   ├── channel_strip/        fader + pan + mute of one channel, smoothed
│   ├── master_fader/         smoothed master volume
│   └── peak_meter/           level meter values
│
├── effects/                one folder per effect, each depends on core only
│   ├── bitcrusher/
│   ├── tremolo/
│   ├── soft_clip/            the master limiter
│   └── _template/            ◄ copy this to start a new effect
│
├── engine/                 the signal graph: sources → strips → master, + hook points
│   ├── engine_init.c
│   ├── engine_render.c       the graph, top to bottom          (audio thread)
│   ├── engine_commands.c     applies queued commands           (audio thread)
│   └── engine_control.c      the engine_* API you call         (control thread)
│
├── tracks/                 TRACKS: one mixer channel + one instrument each
│   └── track_list.c          select, volume, pan, mute, solo, hold
│
├── instruments/            one folder per playable instrument
│   ├── instrument.h          the interface every instrument implements
│   ├── keyboard_piano/       keys (…_keys.c) and sound (…_patch.c) kept apart
│   ├── layered_keyboard/     piano + strings + synth layered; one table row per layer
│   └── _template/            ◄ copy this to start a drumpad, guitar, …
│
├── input/
│   ├── key_event.h           AT_KeyEvent + names for the physical keys
│   ├── console_keys/         raw key down/up events from the console
│   ├── piano_keys/           the piano key layout, shared by keyboard instruments
│   └── key_names/            what is printed on a key (for help texts)
├── ui/
│   ├── console_text/         UTF-8 output, in-place status line
│   └── meter_bar/            level meter drawn with characters
├── io/
│   ├── sample_format/        float → int16 / int24 / int32 / float32, interleaved
│   └── wav_writer/           AT_Buffers → WAV file
│
├── backend/
│   └── wasapi/             the DEVICE only: find it, negotiate a format, exchange buffers
│       ├── wasapi_com.c        GUIDs, per-thread COM, error strings
│       ├── wasapi_devices.c    enumeration, device names
│       ├── wasapi_format.c     shared / exclusive format negotiation
│       └── wasapi_stream.c     open, start, stop, wait / acquire / release
│
├── threads/                one folder per thread, init and loop in separate files
│   ├── audio/
│   │   ├── audio_thread_init.c   create/stop the thread; COM, MMCSS priority, flush-to-zero
│   │   └── audio_thread_loop.c   wait for device → render → convert → hand back
│   └── control/
│       ├── control_thread_init.c console into raw key mode, restore on exit
│       └── control_thread_loop.c poll keys → on_key, then on_tick
│
└── app/                    THE PROGRAM: the only folder that names concrete modules
    ├── app_instruments.c     ◄ register new instruments here (one track each)
    ├── app_effects.c         ◄ register new effects here
    ├── app_setup.c           engine + master sound + tracks + effects
    ├── app_keys.c            what a key does          (control thread callback)
    ├── app_status.c          status line + help text  (control thread callback)
    ├── app_live.c            live mode: device → audio thread → control loop
    ├── app_offline.c         --offline rendering
    ├── app_devices.c         --list, --info, opening the chosen device
    └── app_args.c            command line
```

### Who may include whom

Includes always carry the full path (`#include "effects/tremolo/tremolo.h"`), so a file's dependencies are visible in its first lines.

| Module | Includes |
|---|---|
| `core/` | nothing |
| `dsp/*`, `effects/*`, `mixer/*` | `core/` only |
| `synth/` | `core/`, `dsp/oscillator`, `dsp/envelope` |
| `layers/` | `core/`, `mixer/channel_strip` |
| `equalizer/` | `core/`, `dsp/biquad` |
| `engine/` | `core/`, `equalizer/`, `mixer/*`, `effects/soft_clip` |
| `tracks/` | `engine/`, `instruments/instrument.h` |
| `instruments/*` | `engine/`, `input/key_event.h`, `input/piano_keys`, what their sound is built from (`synth/`, `layers/`, an effect) |
| `backend/wasapi/` | `io/sample_format` (for the sample type enum) |
| `threads/audio/` | `backend/wasapi`, `io/sample_format`, `core/` |
| `threads/control/` | `input/console_keys` |
| `app/` | anything |

The engine does not know the synth, any instrument, layers, tracks, the threads or the app. An effect does not know the engine. If you ever need to include "upwards" in this table, the code belongs in a different folder.

---

## 2. Signal flow

```
 Control thread (threads/control → app/app_keys.c → instruments/…)
        │  engine_note_on(), engine_set_hook(), engine_set_param() …
        ▼
 ┌──────────────────┐   lock-free, the audio thread never waits
 │  core/at_queue   │
 └────────┬─────────┘
          ▼  drained at the start of every block
 ═══ Audio thread (threads/audio → engine/engine_render.c) ══════════════════
  per channel that has a source (8 channels):
    source.render ─[H] SOURCE─▶ channel EQ ─[H] CHANNEL─▶ fader + pan ─┐
                                                                        ▼
  master bus:  Σ ─[H] MASTER_PRE─▶ master EQ ─▶ master fader ─[H] MASTER_POST─▶
               limiter ─▶ meter ─[H] OUTPUT─▶ io/sample_format ─▶ device
 ════════════════════════════════════════════════════════════════════════════
  [H] = hook point: an effect gets the raw float buffer and changes it in place.
        4 chained slots per point.
```

### Tracks and layers

Two different ways of having several sounds:

| | Track | Layer |
|---|---|---|
| What it is | a separate mixer channel with its own instrument | one of several sounds inside a single instrument |
| Notes | each track is played on its own | every layer gets the same notes |
| Result | different instruments side by side | one composite, fuller sound |
| Own EQ, effects, fader | yes: the whole channel above | volume, pan, mute, transpose, 2 insert effects |
| Code | `tracks/track_list.h` (control side of an engine channel) | `layers/layer_stack.h` (a source made of sources) |

```
 track 1 ── piano synth ─────────────────────────────────▶ EQ ▶ fader ─┐
 track 2 ── layer stack ─┬─ piano   ▶ inserts ▶ vol/pan ─┐             ├─▶ master bus
                         ├─ strings ▶ inserts ▶ vol/pan ─┼▶ EQ ▶ fader ─┘
                         └─ synth   ▶ inserts ▶ vol/pan ─┘
```

A layer stack is an ordinary source, so the engine needs no special case for it, and a stack can itself be a layer of another stack.

Keys in live mode (QWERTZ labels):

| Key | Does |
|---|---|
| `Tab` | next track (the keyboard plays the selected track) |
| `,` / `.` | volume of the selected track |
| `9` / `0` | mute / solo the selected track |
| `Enter` | hold: the track's notes keep sounding after you release the keys, also while you play another track. `Enter` again stops them |
| `1`..`3` on track 2 | switch a layer on/off, also while notes sound |
| `N` / `M`, `B`, `Space`, `Esc` | master volume, master EQ on/off, all notes off, quit |

To hear tracks together: go to track 2, press `Enter`, play a chord, press `Tab`, play the piano over it.

Three contracts make everything pluggable (`core/at_buffer.h`, `core/at_module.h`):

| You write | Shape | Plug it in with |
|---|---|---|
| an effect | `void fx_process(AT_Buffer* buf, void* user)` | `engine_set_hook(engine, point, channel, slot, fx_process, &state)` |
| a sound generator | one `const AT_SourceType` (render + note handlers) | `engine_set_source(engine, channel, &MY_SOURCE, &state)` |
| a live parameter | `void x_set_param(void* self, int param, float value)` | `engine_set_param(engine, x_set_param, &state, PARAM, value)` |

---

## 3. How to add things

### A new effect (e.g. `delay`)

1. Copy `src/effects/_template/` to `src/effects/delay/`, rename the two files to `delay.h` / `delay.c`.
2. Rename `AT_TemplateEffect` / `template_effect_*` to `AT_Delay` / `delay_*`.
3. Put parameters and memory into the struct, the DSP into `delay_process()`.
4. In `src/app/app_effects.c`: include the header, add `static AT_Delay s_delay;`, then in `app_effects_setup`:
   ```c
   delay_init(&s_delay, sample_rate, 0.25f);
   add(app, "delay", AT_SC_F1, AT_HOOK_CHANNEL, 0, 1, delay_process, &s_delay);
   ```
   That gives it a hotkey, a status line label and `--fx delay`.
5. `build.bat`. Test without a device: `audiatron --offline test.wav 3 --fx delay`.

### A new instrument (e.g. `drumpad`)

1. Copy `src/instruments/_template/` to `src/instruments/drumpad/`, rename the files and the `template_instrument_*` names. The template already is a working 4-pad instrument.
2. Fill in the key table (playing) and `attach` (sound).
3. In `src/app/app_instruments.c`: include the header, add `static AT_Drumpad s_drumpad;`, then:
   ```c
   drumpad_init(&s_drumpad, sample_rate);
   track_list_add(&app->tracks, drumpad_instrument(&s_drumpad));
   ```
   It gets its own track (the next free mixer channel). **Tab** selects the track the keyboard plays.

### A new layer in the layered keyboard

Add one row to the table in `instruments/layered_keyboard/layered_keyboard_patch.c`:

```c
//    name     waveform        attack  decay  sustain release   detune  transp  volume   pan   trem Hz  depth
    { "bell",  AT_WAVE_SINE, { 0.001f, 1.20f, 0.00f,  0.80f },   0.0f,   12,    0.40f, 0.00f,  0.0f,   0.00f },
```

It gets the next number key for on/off and appears in the status line and the help text. Up to 8 layers.

### A layered instrument of your own

A layer can be any source, not only a synth. Build the stack before plugging it in:

```c
layer_stack_init(&stack, sample_rate);
AT_Layer* low = layer_stack_add(&stack, "low", &AT_SYNTH_SOURCE, &synth_a);
low->transpose    = -12;
low->strip.volume = 0.6f;
layer_stack_add(&stack, "high", &AT_SYNTH_SOURCE, &synth_b);
layer_stack_set_insert(&stack, 1, 0, tremolo_process, &tremolo);     // effect on "high" only
engine_set_source(engine, channel, &AT_LAYER_STACK_SOURCE, &stack);
```

While it plays, change a layer with `engine_set_param(engine, layer_stack_set_param, &stack, AT_LAYER_PARAM(1, AT_LAYER_PARAM_MUTE), 1.0f)`.

### A new sound generator (e.g. a sampler)

Create `src/sampler/`, write the DSP, then one adapter file like `synth/synth_source.c` that fills an `AT_SourceType`. Any instrument can then own a sampler instead of a synth. The engine needs no change.

### Changing only how the piano sounds

Edit `instruments/keyboard_piano/keyboard_piano_patch.c`. Key handling is in `keyboard_piano_keys.c` and stays untouched.

### Rule for file names

`.c` file names must be unique across all folders (all objects go to `build\obj\`). `build.bat` stops with a clear message if two collide.

---

## 4. Debugging

| Symptom | Look here first |
|---|---|
| No sound at all | `audiatron --info`, then `backend/wasapi/wasapi_stream.c` |
| Format / exclusive mode refused | `backend/wasapi/wasapi_format.c` |
| Crackling, `xruns` counting up | `threads/audio/audio_thread_loop.c`; then check the audio-thread rules below in whatever you added last |
| A key does the wrong thing | `app/app_keys.c` (global and track keys), `instruments/<name>/…_keys.c`, `input/piano_keys/piano_keys.c` (note layout) |
| Notes hang or get cut off | is the track on hold (`hold` in the status line)? Then `synth/synth_voices.c`, `dsp/envelope/adsr.c` |
| A track is silent | `mute` / `solo` in the status line, then `tracks/track_list.c` (`track_is_audible`) |
| A layer is missing, too loud or out of tune | the table in `instruments/layered_keyboard/layered_keyboard_patch.c`, then `layers/layer_stack.c` |
| Wrong tone / filter | `dsp/biquad/biquad.c`, `equalizer/equalizer.c`, the instrument's `…_patch.c` |
| An effect misbehaves | its own folder. It cannot be affected by anything else |
| Order of processing | `engine/engine_render.c` (short, reads top to bottom) |
| A command has no effect | `engine/engine_commands.c` (audio side), `engine/engine_control.c` (control side) |

- **Offline first.** `--offline out.wav` runs the same engine, instruments and effects on the main thread, with no device and no real-time pressure. Set breakpoints anywhere, open the WAV in Audacity.
- **Debug build.** `build.bat debug` turns off optimization and enables runtime checks; `build\audiatron.pdb` is there for the debugger.
- **One module in isolation.** Because a module only includes what the table in §1 lists, a 30-line test `main()` plus the module's folder (and `core/`) compiles on its own.

---

## 5. Rules for the audio thread

Everything called from `engine_render`, i.e. every effect `_process`, every source `render`, every `_set_param`:

1. **No** `malloc`/`free`, **no** `printf`, **no** file I/O, **no** locks, no `Sleep`. Each can block for milliseconds, which you hear as a click.
2. Allocate before plugging in. A module's memory lives in its struct (the `user` / `self` pointer) and must outlive its registration.
3. From the control thread, never write into a struct the audio thread is using. Use `engine_*()` calls; for parameters `engine_set_param()`.
4. Every COM interface gets exactly one `Release()`. In `backend/wasapi/` the `goto fail` pattern takes care of it.

---

## 6. Roadmap

**Done**
- [x] WASAPI backend: device list, shared (float32, auto-convert) and exclusive (format probing, minimum period, alignment retry)
- [x] Event-driven audio thread with MMCSS "Pro Audio", overrun counter, device-lost flag
- [x] Engine with pluggable sources, 5 hook points × 4 slots, lock-free commands
- [x] Synth (5 waveforms, PolyBLEP, ADSR, voice stealing), 8-band EQ, mixer, soft-clip limiter
- [x] Offline rendering into WAV
- [x] Layers (several sources as one composite sound) and tracks (select, volume, pan, mute, solo, hold)

**Next** (each is one new folder)
- [ ] `dsp/lfo/`, `dsp/svf/`: LFO and state-variable filter as building blocks
- [ ] `synth/`: second oscillator, per-voice filter, exponential envelopes
- [ ] `layers/`: key ranges (splits) and velocity ranges per layer
- [ ] `tracks/`: a sequencer or looper, so tracks play without the keyboard
- [ ] `effects/delay/`, `effects/reverb/`, `effects/chorus/`, `effects/compressor/`
- [ ] `sampler/` + `io/wav_reader/`: port of the WAV playback from `winmm.h` (reuse its SIMD converters, target float)
- [ ] `instruments/drumpad/`, `instruments/guitar/`
- [ ] `equalizer/`: coefficient smoothing; frequency response from `biquad_design()` for a graphical EQ
- [ ] `engine/`: sample-accurate events (frame timestamp in `AT_Command`), send/return busses
- [ ] `input/midi/`: `midiInOpen` → `engine_note_on`
- [ ] `backend/wasapi/`: `IMMNotificationClient` (hot-plug), capture (`IAudioCaptureClient`)

---

## 7. From WinMM to WASAPI

| winmm.h | Here | Why it is better |
|---|---|---|
| `audio_init()` + `audio_pump()` every 3 ms | `backend_open()` + `audio_thread_start()` | The device wakes the thread by event, no polling. MMCSS priority |
| 3 × 2205 frames ≈ 150 ms latency | 10 ms (shared) / 3 ms (exclusive) | Direct hardware buffer |
| `short` interleaved, clamp in every `mix_stereo` | `float` planar, converted once in `io/sample_format` | No clipping between stages, headroom for EQ |
| `CRITICAL_SECTION` in the mixer | `core/at_queue` (lock-free) | The UI can never block the audio thread |
| `play_tone(freq, gain)` | `engine_note_on(e, ch, note, vel)` | Voice management, ADSR, retrigger instead of stacking |
| phase lock via `g_mix_clock` | `AT_Buffer.frame_clock` in every effect and source | Absolute sample time available everywhere |
| `angle_of_tone()` | `engine_set_channel_pan()` | Per channel, smoothed |
| `reverb_tone()` | an effect folder + a hook point | Any effect, anywhere in the chain |
| `set_amp_tone()` | `engine_set_channel_volume()` / velocity | Smoothed, no zipper noise |
| `play_*_WAV()` | roadmap: `sampler/` as an `AT_SourceType` | |

---

## 8. Reading list

**Microsoft Learn** (search terms)
1. *About WASAPI*
2. *Stream Management*: lifecycle `Initialize → Start → GetBuffer/ReleaseBuffer → Stop`
3. *Rendering a Stream*: the basic loop (here: `audio_thread_loop.c` + `wasapi_stream.c`)
4. *Exclusive-Mode Streams*: event mode, periods, the `BUFFER_SIZE_NOT_ALIGNED` dance
5. *IAudioClient::Initialize* (reference): all flags and error codes
6. *Multimedia Class Scheduler Service*

**Windows SDK headers** (`C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um\`)
- `audioclient.h`, `mmdeviceapi.h`: search for `IAudioClientVtbl` to see the exact C signatures

**Open source**
- **miniaudio.h**: search for `ma_device_init__wasapi` and `AUDCLNT_SHAREMODE_EXCLUSIVE`
- **PortAudio** `src/hostapi/wasapi/pa_win_wasapi.c`: buffer size calculation, hardware workarounds

**DSP**
- Robert Bristow-Johnson: *Audio EQ Cookbook* (the source of `dsp/biquad/biquad.c`)
- Välimäki et al.: *PolyBLEP* (band-limited oscillators, `dsp/oscillator/oscillator.h`)
- Will Pirkle: *Designing Software Synthesizer Plug-Ins in C++* (concepts carry over to C)
- musicdsp.org: filters, oscillators, effects as snippets
- Julius O. Smith: *Introduction to Digital Filters* (free online)
