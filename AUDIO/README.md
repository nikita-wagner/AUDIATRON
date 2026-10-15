# AUDIATRON: raw-C audio engine for Windows (WASAPI + WinMM)

Successor of the WinMM engine (`../WINMM/winmm.h`). Plain C11, no C++, no libraries. Built so that every part is its own folder, can be reused on its own, and new parts can be added without touching existing ones.

One engine, two device backends: **WASAPI** (Windows Vista and later) and **WinMM** (every Windows since 95). The program picks one when it starts; synth, EQ, mixer, effects, tracks and layers are the same code on both.

```
build.bat                           → build\audiatron.exe        both backends (build.bat debug for a debug build)
build.bat winmm                     → build\audiatron_winmm.exe  WinMM only: no WASAPI code, no COM
audiatron                           live: keyboard instruments, shared mode, default device
audiatron --exclusive               exclusive mode (3 ms period on the Razer headset)
audiatron --list                    list output devices
audiatron --device 2 --exclusive    pick a device
audiatron --info [--exclusive]      only negotiate the format and print it
audiatron --backend winmm           use WinMM even where WASAPI exists (default: auto)
audiatron --backend winmm --queue-ms 200     WinMM with a longer queue (if it crackles)
audiatron --fx crush --fx tremolo   start with effects switched on (on the start track only)
audiatron --track 2                 start on track 2 (piano + strings + synth layered)
audiatron --offline out.wav 5       render a demo into a WAV file, no device needed
audiatron --offline out.wav 5 --track 2      the same demo played on track 2
audiatron --play song.mp3           add a player track for a WAV / MP3 file (play, pause, seek, loop, pitch)
audiatron --sample kick.mp3         add a track that plays the file pitched across the piano keys
audiatron --play song.mp3 --fx echo --pitch 3 --keep-tempo     echo on, 3 semitones up, same tempo
audiatron --pad kick.mp3 --pad snare.wav --pad loop.mp3         a drumpad: one file per key, each pitched / looped on its own
audiatron --replay recordings\rec_001.txt                        play an input recording back (on copies of the tracks; F8 again)
audiatron --machine engine.wav                                   the machine track's sound (W / S throttle; default: a built-in petrol engine)
```

Live: **F5** records every key and drum state into a text file, **F6** records a WAV, **F7** both at once (see *Recording and replay*).

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
├── sampler/                plays a decoded WAV / MP3: 8 voices, pitch, loop, play / pause / seek, part of a file
│   ├── sampler.c             voices, transport, cubic-interpolated reading, rendering
│   └── sampler_source.c      lets the engine use a sampler as a source
├── layers/                 LAYERS: several sources played together as ONE sound
│   ├── layer_stack.c         per layer: key range → source → inserts → volume/pan, all mixed
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
│   ├── echo/                 delay with feedback, damping, ping-pong
│   ├── pitch_shift/          pitch change without speed change (granular)
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
│   ├── sample_player/        a song / loop: transport keys, tape or keep-tempo pitch
│   ├── sample_keyboard/      a file pitched across the piano keys (kick → bass line)
│   ├── drumpad/              16 pads, one file each; per-pad pitch, loop, volume, pan, effects, cut
│   ├── replay/               plays an input recording back, on time (the app plays it on copies of the tracks)
│   ├── machine/              mechanical sounds on a throttle: W / S, curves for gain, loop part and rate
│   ├── audio_track/          sound files on a timeline: regions cut, stretched, reversed, faded (for an arrangement)
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
│   ├── wav_writer/           AT_Buffers → WAV file
│   ├── audio_clip/           a sound file decoded into memory (AT_Clip), shared by all readers
│   ├── wav_reader/           WAV → AT_Clip (8/16/24/32-bit, float, extensible, odd chunks)
│   ├── mp3_reader/           MP3 → AT_Clip (uses third_party/minimp3)
│   ├── audio_file/           one call that loads whichever format the file really is
│   └── sound_folder/         the WAV / MP3 files of a folder, in name order (drumpad kits)
├── recording/
│   ├── event_log/            the input recording's text format: "seconds what", write and read
│   └── wav_recorder/         engine output → lock-free ring (audio thread) → WAV file (control thread)
├── third_party/
│   └── minimp3/              public-domain MP3 decoder, unmodified (see its README.txt)
│
├── backend/                the DEVICE only: find it, negotiate a format, exchange buffers
│   ├── audio_backend.h       the contract every backend implements (AT_Backend)
│   ├── wasapi/               Windows Vista and later; shared and exclusive mode
│   │   ├── wasapi_backend.c    the AT_Backend adapter
│   │   ├── wasapi_com.c        GUIDs, per-thread COM, error strings
│   │   ├── wasapi_devices.c    enumeration, device names
│   │   ├── wasapi_format.c     shared / exclusive format negotiation
│   │   └── wasapi_stream.c     open, start, stop, wait / acquire / release
│   ├── winmm/                every Windows since 95; waveOut, 16-bit stereo
│   │   ├── winmm_backend.c     the AT_Backend adapter
│   │   ├── winmm_devices.c     enumeration, device names, the default device
│   │   └── winmm_stream.c      open, start, stop, wait / acquire / release
│   └── select/               picks a backend at run time (auto / wasapi / winmm)
│
├── threads/                one folder per thread, init and loop in separate files
│   ├── audio/
│   │   ├── audio_thread_init.c     create/stop the thread; per-thread setup
│   │   ├── audio_thread_priority.c real-time priority: MMCSS if Windows has it, else thread priority
│   │   └── audio_thread_loop.c     wait for device → render → convert → hand back
│   └── control/
│       ├── control_thread_init.c console into raw key mode, restore on exit
│       └── control_thread_loop.c poll keys → on_key, then on_tick
│
└── app/                    THE PROGRAM: the only folder that names concrete modules
    ├── app_instruments.c     ◄ register new instruments here (one track each)
    ├── app_record.c          F5 / F6 / F7: input and WAV recording, loading the replay
    ├── app_effects.c         ◄ register new effects here
    ├── app_setup.c           engine + master sound + tracks + effects
    ├── app_keys.c            what a key does          (control thread callback)
    ├── app_status.c          status line + help text  (control thread callback)
    ├── app_live.c            live mode: device → audio thread → control loop
    ├── app_offline.c         --offline rendering
    ├── app_devices.c         --list, --info, choosing the backend, opening the device
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
| `sampler/` | `core/`, `io/audio_clip` |
| `io/wav_reader`, `io/mp3_reader` | `io/audio_clip` (the MP3 reader also `third_party/minimp3`) |
| `io/audio_file` | `io/audio_clip`, `io/wav_reader`, `io/mp3_reader` |
| `recording/event_log` | nothing |
| `recording/wav_recorder` | `core/`, `io/wav_writer` |
| `equalizer/` | `core/`, `dsp/biquad` |
| `engine/` | `core/`, `equalizer/`, `mixer/*`, `effects/soft_clip` |
| `app/` (recording, replay) | `recording/*`, `instruments/replay`, and the modules whose parameters it names (`app_params.c`) |
| `tracks/` | `engine/`, `instruments/instrument.h` |
| `instruments/*` | `engine/`, `input/key_event.h`, `input/piano_keys`, `input/key_names`, what their sound is built from (`synth/`, `layers/`, `sampler/`, `io/audio_file`, an effect); `instruments/replay` also `instruments/drumpad`, `recording/event_log` |
| `backend/audio_backend.h` | `io/sample_format` (for the sample type enum) |
| `backend/wasapi/`, `backend/winmm/` | `backend/audio_backend.h` |
| `backend/select/` | both backends (the only place that names them) |
| `threads/audio/` | `backend/audio_backend.h`, `io/sample_format`, `core/` |
| `threads/control/` | `input/console_keys` |
| `app/` | anything |

The engine does not know the synth, any instrument, layers, tracks, the threads, the backend or the app. The audio thread does not know which backend it feeds. An effect does not know the engine. If you ever need to include "upwards" in this table, the code belongs in a different folder.

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
| `C` / `V` / `F1` | crush / tremolo / echo on / off **for the selected track only** (every track has its own; on the drumpad track: for the held pad only) |
| `E` + `+` / `-` | echo time of the track +/- 25 ms (hold to keep going); on the drumpad track also its pads' echoes |
| `1`..`3` on track 2 | switch a layer on/off, also while notes sound |
| `F5` / `F6` / `F7` | record input / WAV / both, on and off (works on every track) |
| `F8` | play the last take (or `--replay FILE`) back / stop it |
| `N` / `M`, `B`, `Space`, `Esc` | master volume, master EQ on/off, all notes off, quit |

To hear tracks together: go to track 2, press `Enter`, play a chord, press `Tab`, play the piano over it.

**The track list.** Which tracks there are is a list (`AT_AppTrackSpec`, `options.track_specs`): each entry an instrument with its files, any instrument any number of times, up to 16 tracks. Three pianos with their own sounds, two drumpads with different kits, an engine per car:

```c
AT_AppTrackSpec* t = app.options.track_specs;
t[0] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_PIANO };
t[1] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_PIANO };
t[2] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_DRUMPAD, .pad_folder = "kits\\house" };
t[3] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_DRUMPAD, .pads = { "kick.wav", "snare.wav" }, .pad_count = 2 };
t[4] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_MACHINE, .file = "jet" };          // or a WAV, or my.atengine
t[5] = (AT_AppTrackSpec){ .kind = AT_APP_KIND_AUDIO };
app.options.track_spec_count = 6;
```

AudioStudio builds its arrangement this way. Without a list the console makes its usual one from the command line: piano, layered keys, a track per `--play` / `--sample` file, the drumpad, the machine. Checked: a list of two pianos, two drumpads with different kits, two engines (jet, petrol) and an audio track gave 7 tracks in that order, each with its own synth, kit and sound; a hit recorded on the second drumpad was written as `track 4 pad 2 S hit` and `F8` replayed it on that drumpad's copy (peak 0.091) while the first drumpad's copy stayed silent.

Three contracts make everything pluggable (`core/at_buffer.h`, `core/at_module.h`):

| You write | Shape | Plug it in with |
|---|---|---|
| an effect | `void fx_process(AT_Buffer* buf, void* user)` | `engine_set_hook(engine, point, channel, slot, fx_process, &state)` |
| a sound generator | one `const AT_SourceType` (render + note handlers) | `engine_set_source(engine, channel, &MY_SOURCE, &state)` |
| a live parameter | `void x_set_param(void* self, int param, float value)` | `engine_set_param(engine, x_set_param, &state, PARAM, value)` |

### Playing WAV and MP3 files

```
audiatron --play song.mp3                     a player track (simple transport, for songs and loops)
audiatron --sample kick.mp3                   a sampler track (the file on the piano keys)
audiatron --play a.wav --play b.mp3 --sample kick.mp3      up to four files, each on its own track
```

Each file becomes a **track** after the built-in ones, so it has its own EQ, fader, pan, mute, solo and hold like the piano, and several files play together. The keyboard starts on the first file's track; **Tab** moves between tracks.

**Player track** (`instruments/sample_player/`):

| Key | Does |
|---|---|
| `P` | play / pause |
| `S` | stop and rewind |
| `L` | loop on / off |
| `Left` / `Right` | seek 5 s back / forward (hold the key to keep going) |
| `Up` / `Down` | pitch +1 / −1 semitone |
| `Y` / `X` | speed −5 % / +5 % (the keys labelled Z and X on a US layout) |
| `R` | reset pitch and speed |
| `T` | switch between tape mode and keep-tempo mode |
| `F1` | echo on / off for this track |

**Sampler track** (`instruments/sample_keyboard/`): the piano keys play the file at the pitch of each key, several at once. The key for C in octave 4 plays the file unchanged. `Y` / `X` change the octave, `1` makes notes end when you let go of the key (otherwise a note plays to the end of the file), `2` loops it. Short sounds are what it is for: a kick becomes a bass line, a vocal "ah" becomes a choir.

**Two ways to change the pitch**

| | Tape mode (default) | Keep-tempo mode (`T`, `--keep-tempo`) |
|---|---|---|
| Pitch and speed | tied together, like a record: +12 semitones also plays twice as fast | separate: pitch changes alone, speed (`Y` / `X`) changes alone |
| Done by | the sampler reading the file faster or slower (cubic interpolation) | the same, plus the pitch shifter (`effects/pitch_shift`) correcting it |
| Accuracy | exact | the pitch is exact |
| Sound | clean at any amount | slight flutter on music, strong flutter possible on a single steady tone (see below) |

Measured through the whole program with a pure 440 Hz tone: tape mode at +12 / −12 / speed 1.5 gives 880.0 / 219.9 / 659.9 Hz; keep-tempo at +5 / −7 / +12 / −12 gives 587.2 / 293.7 / 880.0 / 220.1 Hz. With music (`sample/example.wav`) keep-tempo +5 shifted the whole spectrum by exactly +5.00 semitones and kept the loudness contour in time (correlation 0.89 at 100 ms resolution against 0.17 for tape +12, which changes the timing on purpose).

**Known limit of keep-tempo mode:** the shifter overlaps two grains that are not phase-aligned. On music this is a slight flutter. On a single steady tone the grains can partly cancel depending on the tone's frequency: a pure 440 Hz sine with speed 1.5 or 0.75 in keep-tempo mode measured 1.5 % and 0.75 % off, because the main peak is replaced by sidebands about 7 Hz away. For sustained solo tones use tape mode.

**Echo** (`effects/echo/`): 350 ms between repeats, each repeat 45 % of the last, 40 % level, a little darkening per repeat. The first repeat measures 0.40 of the original and the second 0.18 (0.40 × 0.45), as set. The echo tail keeps ringing after you stop. To change the other values edit `echo_setup` in `app/app_effects.c` (`_FEEDBACK`, `_MIX`, `_DAMPING`, `_PING_PONG` can also be sent while it runs with `engine_set_param`).

**Echo time, live and from code.** Hold `E` and press `+` / `-` (main row or numpad): the echo of the track you are on gets 25 ms longer / shorter, from 1 ms up to the delay line's 2730 ms (at 48 kHz). It works while the echo is off too (it starts with that time), and a running echo glides there in ~0.15 s instead of jumping. On a drumpad track it sets the pads' own echoes as well. `E` itself still plays (a note, or pad 11), as when holding a pad to change it. The status line shows `[echo 375ms]`, and while `E` is held the time it changes. Recordings keep it (`track 1 echo_ms 375`, `pad 1 A echo_ms 225`) and the replay plays it.

For a game, as whole milliseconds:

```c
int ms = echo_ms_for_distance(12.5f);            // a wall 12.5 m away: 73 ms (there and back at 343 m/s)
app_set_echo_ms(app, track, ms);                 // a track's echo (and the drumpad's pads on its track)
drumpad_set_echo_ms(&pads, pad, ms);             // one pad's echo
echo_set_time_ms(&my_echo, ms);                  // an echo of your own, before it is plugged in
engine_set_param(&engine, echo_set_param, &my_echo, AT_ECHO_PARAM_TIME, (float)ms);   // ... while it runs
```

Checked with a test through the app's key handler: `E` + `+` four times took the piano from 350 to 450 ms, `+` without `E` changed nothing, `E` + `-` (main row and numpad) went down, it stopped at 1 ms and at 2730 ms; a running echo followed to 375 ms; on the drumpad, a hit with its pad echo set to 600 ms came back 600.5 ms later (the half millisecond is the hit's own 5 ms fade-in reaching the detection level); `app_set_echo_ms(…, echo_ms_for_distance(34.3f))` set the pad's echo to 200 ms.

**Effects are local.** Every effect (`C` crush, `V` tremolo, `F1` echo) exists once per track, sits on that track's channel and is switched for the selected track only; nothing acts on the master bus or on several tracks. An instance is made the first time it is switched on for a track (an echo is ~1 MB, so unused ones cost nothing). Checked by driving the app's own key handling: with crush, tremolo and echo on for the piano, a drumpad hit comes out bit-identical to one without them.

**Files and memory.** WAV: 8 / 16 / 24 / 32-bit PCM and 32 / 64-bit float, any sample rate, mono or stereo (more channels: front left and right are kept); compressed WAVs are refused with a message. MP3: MPEG-1/2/2.5 Layer III, mono or stereo, constant or variable bit rate, with ID3 tags. Which format a file is gets decided by its content, not its name. A file is decoded completely into memory as 16-bit audio at load time: 2 bytes per sample, so a 3-minute stereo song at 44.1 kHz needs about 32 MB. That is fine here and a real limit on a small Windows 98 machine (use short or mono files there); streaming from disk is on the roadmap.

### Drumpad

```
audiatron                                   the drumpad loads the "sample" folder by itself
audiatron --pads D:\kits\808                every WAV / MP3 of a folder, in name order
audiatron --pad "kick.mp3(250ms,1000ms)"    only 250..1000 ms of the file (750 ms, frame-exact)
audiatron --pad "kick.wav(250ms,1000ms,2000ms,1000ms)"   that part, 2 s after a press, sounding for 1 s
audiatron --pad "kick.mp3(0.5)"             only the first half of the file
audiatron --pad "loop.wav(0.2,0.9)"         from 20 % to 90 % of the file (seconds: "loop.wav(0.5s,2s)")
audiatron --pad kick.mp3 --pad snare.wav    single files, pad 1, 2, ...
```

**Started without any sound option** (`--play`, `--sample`, `--pad`, `--pads`), the program looks for a folder named `sample` (in the current folder, one or two folders up, then the same next to the executable, so `build\audiatron.exe` finds `AUDIATRON\sample`) and puts every WAV / MP3 in it on the pads in name order. It prints `drumpad: 4 sound(s) from "…"` and the keyboard starts on the drumpad track. A file that does not decode is skipped with a message.

Every `--pad` file goes on the next pad, then the `--pads` folder fills the remaining pads (up to 16 in all); all pads share **one** drumpad track, after the `--play` / `--sample` tracks. The folder listing is `io/sound_folder/` (`FindFirstFileA`, so it works on Windows 98 too). Each pad is one key and has its **own** pitch, loop, volume, pan and pitch mode.

```
 pads   A S D F G H J K        pads 1..8    (the first 8 --pad files)
        Q W E R T Z U I        pads 9..16   (QWERTZ labels; the help text prints your layout's labels)
```

**Own keys.** That is the default; the keys are not fixed. `drumpad_set_pad_keys(keys)` gives the 16 pads any keys (scan codes, 0 = a pad without a key, `NULL` = the default) and `piano_keys_set_layout(&layout)` does the same for the piano keys of every keyboard instrument (piano synth, layered keys, sampler): a key for each of up to 24 notes (two octaves from the C of the current octave) and the two octave keys. They hold for every track of that kind at once. A bound key plays first on its tracks, so a control key given to a pad hits the pad there; the arrows and the other "extended" keys never play notes or pads (they share scan codes with the numpad). AudioStudio sets them in its **Keys** tab: click a note or a pad, press the key (Esc: no change, Backspace: no key; a key taken from where it was), **Reset to default**, save and load sets as `.atkeys` in the library; a project keeps its bindings and Ctrl+Z / Ctrl+Y undo and redo changes to them. Checked through the studio's own key handling: C moved to `X` played the C of the octave and `A` played nothing, octave up moved to `M`, the comma on the upper B played 23 semitones up, pad 5 moved to `L` was held by `L` and not by `G`; saved, reset, loaded, written into a project and read back, undone and redone, each time the same 42 keys in the engine.

Hold a pad and press a control key to change **that** pad. Hold several pads to change them together. A control key with **no pad held does nothing**, so a change never lands on a pad you did not mean. To change a running loop, hold its key: a press used for a change does not stop the loop.

| Key (while holding a pad) | Does |
|---|---|
| `+` / `-` | pitch +1 / −1 semitone, ±24 (also numpad `+` / `-`, and `=` / `-` on a US keyboard). Hold to keep going |
| `Up` / `Down` or `,` / `.` | volume of this pad (on the drumpad track `,` `.` are not the track volume) |
| `Left` / `Right` | pad pan |
| `L` | loop on / off |
| `P` | tape ↔ keep-tempo pitch, for this pad |
| `C` / `V` / `F1` | crush / tremolo / echo **on this pad only** (on the drumpad track they are not the global effects) |
| `X` | sync: the pad runs on the shared clock (default) or free (its own clock) |
| `/` | timing: type start, end (the part of the file), wait and play for, in ms (see below) |
| `*` | the pattern (step sequencer): no pad needs to be held (see *Pattern* below) |
| `Backspace` | reset the pad: pitch 0, tape, volume 1, centre, effects off |

- **One-shot pad** (default): a press plays the file to its end; pressing again restarts it. Pitch changes reach a hit that is still sounding.
- **Looped pad**: a press starts the loop, the next press stops it. A press that was used to change the pad (hold `A`, press `+`) does **not** stop it. Hold a pad and press `L` to start looping it right away; `L` again lets it finish its current pass and stop. `Space` stops every loop.
- Loops keep running when you `Tab` to another track, like a player track's song.
- Pitch modes work as on the player (below): **tape** (default) is exact and clean, a pitched-up loop also gets faster; **keep tempo** keeps a loop in time but adds the shifter's slight flutter and softens drum transients a little.

How it is built: each loaded pad is a sampler (`sampler/`) with its own pitch shifter as a layer insert, and the pads are the layers of one layer stack (`layers/`), each layer answering only its own note: pad 1 = note 36, pad 2 = 37, … (where the General MIDI drum notes start). From code:

```c
drumpad_init(&pads, sample_rate);
drumpad_load(&pads, 0, "kick.mp3", error, sizeof error);      // before attach
track_list_add(&tracks, drumpad_instrument(&pads));
drumpad_set_pitch(&pads, 0, -3.0f);                           // any time afterwards
drumpad_hit(&pads, 0, 1.0f);                                  // = engine_note_on(engine, channel, 36, 1.0f)
drumpad_loop_start(&pads, 1);
```

**Synced and free pads (the clock).** The drumpad has one shared clock. Each pad is either **synced** (default) or **free** (`X` while holding it, `drumpad_set_sync(&pads, pad, false)` from code):

| | synced | free |
|---|---|---|
| Clock | the drumpad's one shared clock | its own, starting when the pad starts |
| Starting a loop | the first synced loop starts the clock (from its top); a synced loop started later drops in where it would be had it started with the clock, so all synced loops stay phase-locked, however late you press them | from its top, the moment you press it |
| Tremolo LFO | runs on the shared clock (in phase with the other synced pads) | starts its cycle when the pad starts |

So pads 1 and 3 can be locked while pad 2 runs free. When no synced loop is playing, the next synced loop restarts the clock. One-shot hits always play the moment you press them. A synced loop pitched in tape mode plays faster or slower and so leaves the clock's phase; keep-tempo pitch (`P`) keeps it locked. Measured with the test program: a synced loop pressed 0.25 s after the first one starts at exactly the clock's phase, and 3 s later both are still on it to the sample; a free loop started meanwhile counts from its own start; a synced loop stopped and pressed again re-joins in phase.

**Every pad is its own layer**: its own volume, pan, pitch, effects and clock. Each pad renders into its own buffer, runs its pitch shifter and effects there, and only the finished result is added to the shared output buffer; no part of one pad's processing touches another's. (Running each pad on its own thread would not separate them further, since that state is already separate, but it would add latency and timing jitter between pads and gives nothing on a single-CPU Windows 98 machine.) An echo on the snare leaves the kick dry, and each pad's echo tail is its own. The effects sound like the global ones (same settings, in `drumpad_effects.c`) and sit in the pad's layer after its pitch shifter. From code: `drumpad_set_fx(&pads, 1, AT_PAD_FX_ECHO, true)`, `drumpad_set_volume(&pads, 1, 0.5f)`.

**A pad's timing (`/`): which part, when, how long.** Hold a pad, press `/`, and type four values in milliseconds, Enter after each:

```
hold F, /         status line:  F 1/4 start ms (file 2350): _
2 5 0 Enter                     F 2/4 end ms (250.., now 2350): _
1 0 0 0 Enter                   F 3/4 wait ms before it plays (now 0): _
2 0 0 0 Enter                   F 4/4 play for ms (0 = part length, now 0): _
1 0 0 0 Enter                   F ... 250-1000ms wait2000 for1000
```

| Value | Example | What it does |
|---|---|---|
| start, end | 250, 1000 | the part of the file that plays (750 ms here) |
| wait | 2000 | after a press (or a loop start), silence this long, then it plays |
| play for | 1000 | it sounds this long: shorter than the part = cut off (5 ms fade), longer = silence after the part; 0 = as long as the part. **A looped pad repeats every "play for"**, also when that is longer than the sample: a 400 ms kick with play for 1000 sounds 400 ms, is silent 600 ms, sounds again |

So the example plays, 2 s after the press, 250..1000 ms of F's file, then is silent until 1 s has passed; looped, again every 1 s (at 2, 3, 4 s ...). Pads with the same "play for" loop in step whatever their files, and a synced pad joining late lands on the same repetitions. Times are real time (a pitched pad keeps its rhythm) and frame-exact: in the test, the example started on frame 96000 (2.000 s), a 400 ms sample looped every 1000 ms repeated at frames 48000 and 96000, and with a 500 ms wait at 24000 and 72000; from the command line the sound started at 2.000, 3.000 and 4.000 s.

An empty Enter keeps a value, so `250 Enter 1000 Enter Enter Enter` only cuts and `Enter Enter 2000 Enter 1000 Enter` only times; `0` switches wait / play for off. From code: `drumpad_set_cut_ms(&pads, pad, 250, 1000); drumpad_set_timing(&pads, pad, 2000, 1000);` In a recording: `pad 4 F timing 2000ms 1000ms`. It lives in the sampler (`AT_SAMPLER_PARAM_DELAY_MS` / `_LENGTH_MS`), so any sampler-based instrument can use it.

`/` is found by the character it types: Shift+7 on a German keyboard, `/` on a US one, the numpad `/` on both. Digits from the number row or the numpad; Backspace deletes; Esc cancels everything. While you type, the keys do nothing else (a digit never plays a pad). A start outside the file or an end before the start is refused with a note in the status line.

The cut is in **milliseconds, frame-exact**: the sampler turns them into frames itself (`AT_SAMPLER_PARAM_REGION_FROM_MS` / `_TO_MS`), so 750 ms is 36000 frames at 48 kHz in every file. Pads cut to the same length therefore loop in step for ever, even when their files have different lengths: in the test, a pad cut to 0..300 ms of a 400 ms file and one cut to 100..400 ms of a 600 ms file were still exactly 100 ms (4800 frames) apart after 10 s of looping, and a 250..1000 ms cut was at file frame 45600 after 700 ms and stopped on the frame at 750 ms. From code: `drumpad_set_cut_ms(&pads, pad, 250.0f, 1000.0f)`; in a recording: `pad 4 F cut 250ms 1000ms`.

**Part of a file** (one-shot and loop): `drumpad_set_cut(&pads, 0, 0.0f, 0.5f)` plays the first half, `(0.2f, 0.9f)` from 20 % to 90 %. The same works for player and sampler tracks: `sample_player_set_cut`, `sample_keyboard_set_cut`, and on the command line after any `--play` / `--sample` / `--pad` file: `FILE(TO)`, `FILE(FROM,TO)`, numbers are fractions of the length, with `ms` milliseconds, with `s` seconds (a pad cut given in `ms` or `s` is set frame-exact, as with `/`). One function covers WAV and MP3; the format is found from the file. In PowerShell put the argument in quotes. A cut edge in the middle of a sound fades over 5 ms, and a cut loop dips for 5 ms at its seam, so neither clicks; the whole file (0..1) plays exactly as before. This lives in the sampler (`AT_SAMPLER_PARAM_REGION_FROM` / `_TO`), so every sampler-based instrument has it.

`--offline` with pads starts every pad looping at once. Memory: the file plus about 64 KB (its pitch shifter) per loaded pad, plus about 1 MB for a pad's echo, allocated only the first time echo is switched on for that pad.

Checked with a test program that drives the real engine with simulated key presses and measures the output (440 / 660 Hz test tones): hits reach only their own pad; +12 on a held pad gives 880.0 Hz while the other pad stays at 660.1 Hz; a looped pad keeps sounding past the end of its file; `+` on a held, looping pad pitches it (740.8 Hz) without stopping it; the next plain press stops it, and so does all-notes-off; `L` off finishes the pass; pan and reset work. Also checked: `,` on a held pad halves that pad's level (measured 0.500 of the other pad), echo on one pad rings past the end of its file while the other pad stays dry, crush on one pad leaves the other bit-identical, a pad cut to (0, 0.5) stops at 0.5 s of a 1 s file, and neither a cut end nor a cut loop seam steps further between two samples than the test tone itself does. Not checked by ear or by playing it live in the console.

**Not verified by listening.** Everything above was checked with measurements (frequency, level, timing, click detection), not by ear. Judge the sound of the keep-tempo mode and of the echo yourself.

### Pattern: a step sequencer (`*`)

A grid of bits over the pads: **row 1 is pad 1, row 2 pad 2 ...; every column is one step**. A `1` plays that row's pad on that step, a `0` is a rest. Every step lasts the step time, and the pattern loops.

`*` is Shift+`+` on a German keyboard (or the numpad `*` on the drumpad track). The first time it asks three things on the status line:

```
*                     * pattern size rows x steps (now 4x4): _
4x4 Enter             * step time ms or s (now 1000, 0 = stop, 8x4 = size): _
500 Enter             (the grid is printed)  * row 1/4 A kick.wav (now 0000): _
1010 Enter            * row 2/4 S snare.wav (now 0000): _
0101 Enter            ...
1000 Enter
0001 Enter            → playing:   pattern playing: 4 x 4, 500 ms per step
                                     1 A   kick.wav     1010
                                     2 S   snare.wav    0101
                                     3 D   hat.wav      1000
                                     4 F   (no sound)   0001
```

Once a pattern exists, `*` only asks the step time and then shows every row again to fill (each row with its current bits; an empty Enter keeps it). To change the size, type it at the step time (`8x4`).

- Size: `4x4` (also `4*4`, `4,4`), up to 16 rows (pads) and 32 steps. Empty Enter keeps it.
- Step time: `500` = 500 ms, `0.5s` = half a second; plain numbers below 10 are seconds (`0.5`). From 20 ms to 60 s. `0` stops the pattern (it is kept).
- Rows: one `0` / `1` per step; fewer = the rest are `0`. Backspace deletes, Esc cancels everything (the old pattern stays).
- Finishing starts it; `Space` stops it like everything else. The status line shows `pat 4x4 3/4` while it plays.
- A hit is an ordinary pad hit, so it uses the pad's pitch, cut, wait / play for and effects, and recordings log it (on its exact frame). Set "play for" to the step time to make every hit last exactly one step.

The hits are sent 250 ms ahead with their engine frame (`engine_at`), like the replay's lines, so every step lands on its sample: in the test, steps of 500 ms started on frames +0, +24000, +48000, +72000 with the right pad on each, and recorded hits came out exactly 0.500000 s apart. From code:

```c
uint32_t beat[2] = { 0x5, 0xA };          // row 1: steps 1 and 3; row 2: steps 2 and 4 (bit 0 = step 1)
drumpad_pattern_set(&pads, 2, 4, beat, 500.0f);
drumpad_pattern_play(&pads);
drumpad_pattern_stop(&pads);
```


### Machines: mechanical sounds on a throttle

The machine track (track 4 with the default kit) plays one sound file as an endless loop over a part of the file, and a **throttle** from 0 (idle) to 1 (full) decides how it sounds. A petrol engine, a generator, a drill, a fan: one recording each.

```
audiatron --machine engine.wav       the file to use (WAV / MP3); without it a built-in petrol engine plays
audiatron --machine jet              a built-in sound with its settings: petrol, jet, propeller, rocket
audiatron --machine my.atengine      a saved engine: its sound and whole setup (see Engine files below)
```

| Key (on the machine track) | Does |
|---|---|
| `W` hold | throttle up; starts the machine if it is off |
| `S` hold | throttle down |
| `Q` | machine on / off |
| `R` | throttle back to idle |
| `X` | auto idle on / off: with no key held the throttle sinks back to idle (off: it stays where you leave it) |
| `G` | gearbox: off → manual → automatic → off |
| `A` / `D` | shift down / up |
| `1` `2` `3` `4` | another engine sound, and only the sound: petrol engine, jet, propeller, rocket. Your curves, effects and gearbox stay |
| `5` `6` `7` `8` | factory setup: that sound with the curves, effects and gearbox made for it |
| `P` | save the engine (sound and setup) as `machines\machine_001.atengine`, the next number each time |

Four **curves** turn the throttle into the sound. Each has a value at idle, a value at full throttle and a shape in between (`value = idle + (full - idle) * f(throttle)`):

| Curve | Default (idle → full) | Means |
|---|---|---|
| gain | 0.35 → 1.0 | how loud |
| from | 2.0 s → 2.0 s | where the looped part of the file starts |
| to | 4.0 s → 3.0 s | where it ends: idle loops 2.0..4.0 s, full throttle 2.0..3.0 s |
| rate | 1.0 → 1.0 | how fast the file is read (speed and pitch together; e.g. 1 → 1.9 makes an engine rev) |

The machine also has **its own effects**, each off until switched on (`machine_set_fx`), and every one of their settings is a curve on the same throttle, so the sound changes as a whole when you open it up:

| Effect | Curves (default idle → full) |
|---|---|
| EQ | low-pass 1800 → 14000 Hz (the muffler opens, ease in), low shelf 120 Hz +3 → +1 dB, bell 300 → 1500 Hz at 0 → +4 dB, high shelf 6 kHz −4 → +2 dB |
| crush | downsample 1 → 6, bits 12 → 6 |
| tremolo | rate 6 → 22 Hz, depth 0.5 → 0.15 (its own phase, so a changing rate never jumps) |
| echo | time 140 → 70 ms, feedback 0.35 → 0.25, mix 0.25 → 0.15 (~1 MB, made the first time it is switched on) |

They run in that order after the loop reader, follow the smoothed throttle every 32 frames, and keep running 3 s after the machine stops so the echo rings out. A recording has them as `track 6 machine fx_eq 1` and `track 6 machine eq_lowpass_full 9000`. Checked: with the low-pass on the throttle (300 Hz → 15 kHz) the share of energy above 1 kHz went from −50.8 dB at idle to −29.6 dB at full; all four effects on during a sweep stayed finite and below full scale; a take with the machine's EQ, tremolo and echo switched on, a changed curve and the throttle at 0.8 came back exactly on the replay's copy.

**Built-in sounds** (`instruments/machine/machine_builtin.c`), synthesized when chosen, each even all the way through so any part loops cleanly, each with a preset (`machine_preset`) that makes it move:

| Sound | What it is | Preset |
|---|---|---|
| petrol | four-cylinder car engine: exhaust pulses, muffler, valve train | rate 0.8 → 2.4 on the engine's rev, muffler (EQ) opens, automatic gearbox, pedal lifts when W is let go |
| jet | turbofan: roar, rumble, fan whine at 3.15 kHz and harmonics, hiss | spools slowly (follow 900 ms), rate 0.75 → 1.45, low-pass 2.5 → 16 kHz, whine bell +4 dB |
| propeller | 3-blade propeller at 2300 rpm: blade-pass pulses at 115 Hz, chopped air, a nine-cylinder radial's exhaust | rate 0.6 → 1.3, low-pass 1.8 → 9 kHz |
| rocket | heavy low roar, mid roar, hiss, crackle of sparse sharp impulses, slow flutter | rate 0.85 → 1.1, low-pass 3.5 → 18 kHz, echo off the pad |

**Gearbox** (`instruments/machine/machine_gearbox.c`). Off, the curves read the throttle. Manual or automatic, the throttle is a vehicle's gas pedal and the curves read the **engine speed** (rev: 0 at idle rpm, 1 at the rev limiter), which has momentum:

- The free engine (neutral, clutch open) moves toward the pedal's rpm with `rev_up_s` / `rev_down_s`. In gear, rpm = speed × the gear's ratio; the pedal accelerates the vehicle (`power`, less in taller gears and near the limiter, none on it), `drag` and `engine_brake` slow it; pulling away, the clutch slips.
- **A shift**, with the settings of the gear it goes into: clutch out (`clutch_out_ms`, the pedal lifted by `lift`), gear change (`shift_ms`, the engine runs free: it falls on an upshift and blips up to the new gear's rpm on a downshift), clutch in (`clutch_in_ms`): the rpm moves to the new gear's along that gear's **momentum curve**, a shape over the clutch-in time (ease out: quick, then settling). Every gear has its own ratio, shift points, times, lift and curve.
- **Automatic**: up at the gear's `up_rpm` × the **shift-point curve** at the pedal (default 0.5 → 1, ease in: a light foot shifts early, full pedal at the limit), down below `down_rpm`, kickdown at full pedal, and, like a real automatic, an early upshift when you lift off. **Manual**: `A` / `D`, below 1st is neutral.
- Defaults: a six-speed car, 900–6800 rpm, ratios 130 / 78 / 55 / 42 / 34 / 28 rpm per km/h, about 185 km/h flat out.

The rev is sent to the sound like the throttle, so a recording has the engine as it sounded and the replay needs no gearbox. Checked by driving the gearbox through the real engine in 10 ms steps: full pedal goes 1 → 6 in order with no downshift (183 km/h in 6th after 60 s), the rpm stays between idle and the limiter, every upshift drops the revs, a shift takes exactly clutch out + shift + clutch in (0.45 s against 0.43 s set, in 10 ms ticks), while the clutch closes the rpm follows the momentum curve to within 0.001, a 30 % pedal shifts 1 → 2 at 3375 rpm instead of 6198, manual stays in 1st on the limiter until `D`, a slower clutch set on 3rd changes only shifts into 3rd, and lifting off at 150 km/h it coasts down to 14 km/h in 1st.

**Engine files** (`instruments/machine/machine_file.c`, `.atengine`). An engine you set up is plain text, one setting a line, so you can keep a library of engines, edit one in a text editor and load it with `--machine my.atengine` (or in AudioStudio):

```
audiatron engine 1
sound file C:\sounds\diesel.wav               or: sound builtin jet
volume 1
follow_ms 120
rise 0.6
curve rate 0.8 2.4 ease_in 1.5 0.25 0.5 0.75     name, idle, full, shape, amount, three custom points
fx eq on
gearbox gears 6
gear 1 130 6200 0 70 140 220 0.85 0 1 ease_out 2 0.25 0.5 0.75
gearbox mode automatic
```

`gear` lines are: number, ratio, up rpm, down rpm, clutch out / shift / clutch in ms, lift, then the momentum curve like any curve (idle, full, shape, amount, three points). Numbers are written in their shortest exact form, so a file loads back bit for bit. A sound file that is not where the line says is also looked for next to the `.atengine`, so a folder with an engine and its WAV can be moved as a whole. Lines a reader does not know are skipped. From code, the parts can be chosen, e.g. only the gearbox to put under another engine:

```c
machine_save_preset(&m, "v8.atengine", AT_MACHINE_SAVE_ALL, error, sizeof error);
machine_load_preset(&m, "v8.atengine", AT_MACHINE_SAVE_GEARBOX, error, sizeof error);     // SOUND, CURVES, FX, MOTION, GEARBOX
machine_write_settings(&m, f, AT_MACHINE_SAVE_SETUP);      // into a file of your own; machine_read_setting reads a line back
machine_settings_snapshot(&m, AT_MACHINE_SAVE_SETUP, emit, user);   // the same lines, one call of emit each
```

Checked: a machine with a WAV of its own and every curve, effect, motion setting, the gearbox and all 8 gears changed came back from its file bit for bit in a fresh machine; loading only the gearbox, or the setup without the sound, left the rest untouched; a built-in jet with its preset came back as the jet; a hand-written file whose WAV had moved found it next to the file and skipped the lines it did not know; a file without the header, a missing sound and an unknown built-in were refused with a message.

Shapes: `linear`, `ease in` (t^amount: slow, then fast), `ease out` (fast, then slow), `s-curve`, `custom` (three points of your own at 25 / 50 / 75 %). From code:

```c
machine_init(&m, sample_rate);                                    // built-in engine, the defaults above
machine_load(&m, "diesel.wav", error, sizeof error);              // before or after attach (switches between two blocks)
AT_MachineCurve rate = { 1.0f, 1.9f, AT_MACHINE_SHAPE_EASE_IN, 1.5f, { 0.25f, 0.5f, 0.75f } };
machine_set_curve(&m, AT_MACHINE_CURVE_RATE, &rate);
machine_set_running(&m, true);
machine_set_throttle(&m, 0.7f);                                   // what W / S do, in small steps
```

How it sounds: the throttle is smoothed on the audio thread (`follow`, 120 ms), the loop's seam is crossfaded with equal power (40 ms), and when the throttle moves the loop's edges past the read position, the jump is crossfaded too (12 ms), so moving the throttle never clicks. Holding W / S moves the throttle at 0.6 / 0.9 per second (`rise_per_s`, `fall_per_s`); every step is sent with `engine_set_param`, so an input recording (F5) has the throttle as it moved (`track 6 machine throttle 0.45`) and the replay plays it on the machine's copy. Checked with a test that sweeps the throttle idle → full → idle over a 440 Hz tone for 16 s: the largest step between two samples was 0.058 of the peak, which is a 440 Hz sine's own slope at 48 kHz, so no seam and no moving edge clicked. Not judged by ear.

### Sync: tracks on one clock

The drumpad has always had a shared clock for its pads. The engine now has one for whole tracks (`engine/engine_sync.c`): every track switched to sync (`app_set_track_sync`, in AudioStudio the **SY** button on a track) is on it, so what those tracks play starts from the same place in time.

- **Loops join in phase.** A loop that starts on a synced track (a drumpad loop, a song on a player, the machine's loop) asks the clock where it stands and starts there, as if it had started together with the clock: two loops of the same length are at the same position of their buffers, however late the second one was pressed. While nothing synced plays the clock is idle; the next synced loop starts it, from its top. Each source says whether it plays through `AT_SourceType.playing`.
- **Notes and hits wait for the grid.** While the clock runs, notes sent to a synced track wait for its next grid step (`engine_set_sync_grid`; AudioStudio sets 1/16 at the song's tempo by default, 0 = they play at once). A note-off lands at least one step after its note-on, so a quick tap is one step long, never silent. Notes sent with `engine_at` (a sequencer, a replay) are on their frame already and are left alone.
- **A replay** on a synced track starts on the clock's next bar.
- **A transport** can pin the clock to its beat 0 (`engine_set_sync_origin(e, frame, true)`): then it runs while the song runs. AudioStudio does that, and when a synced loop is already running when Play is pressed, the song starts on one of the clock's bars, so the song and the loop stay together.

Checked offline (scratchpad test, 48 kHz): with the grid at 6000 frames from frame 1000, a note sent at frame 6720 sounded at 7000 and a quick tap's note-off was put on 13000; with sync off the note sounded at once. A player loop started the clock; a drum loop of the same length started 11040 frames later played at exactly the player's position, block after block (10080 = 10080). A synced machine started at the clock's place in its loop (head 2.4800 s, computed 2.4800 s). With the track's sync off a drum loop started from its top. Input recordings (F5) keep the notes as they were played, before the grid: the replay plays them unquantized.

Tracks: 12 at most now (24 engine channels: every track and its replay copy).

### The audio track: sound files on a timeline

`instruments/audio_track/` plays REGIONS of any number of WAV / MP3 files: a part of a file (its cut), for a given time, stretched, reversed, faded, with a gain and a pitch. Up to 16 overlap. It plays nothing by itself: a sequencer (AudioStudio's arrangement) sends `audio_track_play(track, &region, frame)` after `engine_at`, and the region starts on that exact frame; the region is copied into a voice when that command arrives, so the control thread never writes what the audio thread reads. The app adds the track only when asked (`options.audio_track`; AudioStudio does), since the console has no timeline. Its regions are not in input recordings; a WAV recording (F6) has them.

```c
AT_AudioRegion r = { .file = audio_track_load(&track, "loop.wav", err, sizeof err),
                     .offset_s = 0.5, .length_s = 2.0, .rate = 0.5, .gain = 1.0f,
                     .keep_pitch = true, .fade_out_s = 0.1f };     // 1 s of the file over 2 s, same pitch
engine_at(&engine, start, tag);
int slot = audio_track_play(&track, &r, start);
engine_at(&engine, 0, 0);
```

- **Stretch**: `rate` = seconds of file per second of region. Like tape the pitch goes with the speed (read directly, cubic); with `keep_pitch` only the speed changes: grains of the file read at the pitch, placed at the speed, two sine² windows that add up to 1 (like the pitch shifter, but reading the file itself, so with no added delay). `pitch` shifts on top, the length stays.
- **Start inside it**: `start_s` (a transport starting in the middle of a region goes on from there).
- Every start and stop is faded over 3 ms on top of the region's own fades, so a cut in the middle of a sound does not click.

Checked offline: a region sounded from its exact frame for its exact length (12000 frames of 0.25 s); a 440 Hz file stretched x2 like tape measured 880 Hz (+54 dB over 440), stretched x2 and x0.5 keeping the pitch stayed at 440 Hz (+33 / +35 dB), pitch +12 at the same length gave 880 Hz, all 24000 frames long as set; a rising ramp reversed fell; a region started half way in began at the ramp's middle; a stop faded out within 142 frames (3 ms).

**A part of a recording** (`replay_play_part`): a replay can play its recording from a time on, faster or slower (event times scaled), from an exact frame until another. Settings recorded before that time are applied at its start, its notes from before are left out. AudioStudio puts input recordings (.txt) on the audio track's lane this way. Checked: a part from 1 s at x2 applied the volume set at 0.5 s at its start, left out the note at 0.5 s, played the note at 1.5 s 0.25 s after its start and stopped at its end frame without the note at 3 s.

### Recording and replay

| Key | Records | File |
|---|---|---|
| `F5` | input: every key and every drum state, as text | `recordings\rec_001.txt` |
| `F6` | audio: exactly what goes to the speakers | `recordings\rec_001.wav` |
| `F7` | both, started and stopped on the same frame | both, same number |

Press the same key again to stop. Input and WAV can also be started and stopped separately (`F5` while `F6` runs: the text joins the running take's number). Takes are numbered; an existing file is never overwritten. `--record-dir DIR` saves elsewhere. The status line shows `REC in 0:12 wav 0:12` while recording.

**The input file** is plain text, one event per line, `seconds what`:

```
# AUDIATRON input recording. One event per line: seconds since the start, then what happened.
audiatron recording 1
rate 48000
pad 1 A file C:\AUDIATRON\sample\kick.wav        ← the sounds on the pads (full paths)
pad 2 S file C:\AUDIATRON\sample\loop.mp3
0.000000 pad 1 A pitch +0                         ← the state of every pad at the start
0.000000 pad 2 S sync on
...
0.000000 wav start recordings\rec_001.wav         ← where the WAV begins, on this time line
0.100000 key A down                               ← what you pressed ...
0.100000 pad 1 A hit 1                            ← ... and what the drumpad did
0.100000 key A up
0.350000 pad 2 S loop start                       ← a loop starts here
0.800000 pad 1 A pitch +3                         ← pitched
0.800000 pad 1 A echo on                          ← an effect switched
1.500000 pad 2 S crush on
2.000000 pad 2 S loop stop                        ← the loop ends here
2.300000 all notes off
2.600000 end
```

**Every track is recorded**, not only the drums. The header says what each track is (`track 1 piano`, `track 2 layers`, `track 3 player file C:\music\song.mp3`, `track 4 drumpad`), the first lines at time 0 hold the state of everything, and then:

```
1.050000 track 1 note 60 on 0.800000012     ← piano: a note starts ...
1.300000 track 1 note 60 off                ← ... and ends
1.300000 track 1 synth wave 1               ← the piano's waveform (key 2)
1.550000 track 2 layers 2 mute 1            ← layered keyboard: layer 2 (strings) off
1.550000 track 2 note 67 on 0.800000012     ← a chord note on the layers
0.100000 track 3 sampler play               ← the song starts
0.350000 track 3 sampler rate 1.05946314    ← pitched up a semitone (tape mode)
0.350000 track 3 shifter ratio 1            ← (keep-tempo mode uses the shifter)
1.550000 track 2 volume 0.9                 ← track volume, mute / solo / hold, crush / tremolo / echo
1.600000 track 1 tremolo rate 4.5           ← a track effect's settings: tremolo rate / depth,
1.600000 track 1 crush bits 7               ←   crush downsample / bits, echo feedback / mix / damping / ping_pong
1.700000 track 2 layers 2 tremolo depth 0.3 ← inside a layer: its insert effect ...
1.700000 track 2 layers 3 synth attack 0.01 ← ... and its synth
```

Effects are recorded with all their settings: at the start of a take every effect instance a track has writes its settings (and `on` if it is switched on), a change while recording writes a line, and an effect switched on for the first time during the take writes the settings it starts with, so the replay's copy starts it the same way. (The echo's time is the `echo_ms` line.) Checked with a test that records tremolo, crush and echo changes, a layer's tremolo and a layer's synth, replays the take and compares the copies: every value came back exactly.

These lines come from the engine itself: every note and every parameter change an instrument sends (to its synth, layer stack, sampler or pitch shifter) is seen there (`engine_set_observer`) and written with its track. So a new instrument built from these modules is recorded without any extra code. Drum lines carry their drum track (there may be several): `track 4 pad 2 S hit 0.9`, `track 4 pad 1 A tempo on/off`, `volume 0.8`, `pan -0.2`, `tremolo on/off`, `sync on/off`, `cut 0.2 0.9`, `loop on/off`, `reset`. (Recordings from before name no track, `pad 2 S hit`: they play on the first drum track.) The app's own changes: `track 3 select drumpad`, `master volume 0.9`, `all notes off`. Values are written with 9 digits, so the replay sets exactly the same numbers; times have 6 decimals, so every event goes back on its exact frame.

**Playing a recording back.** `F8` plays the last take (it is loaded when `F5` stops) and stops it again; `--replay FILE` plays a recording from the start. There is no replay track any more: in AudioStudio recordings are regions on an audio track, cut and stretched like sound files (`replay_play_part`), and a take recorded there lands on an audio track by itself.

It plays **everything on copies of the tracks**: every track (piano, layers, players, sampler keyboards, drumpads, machines) has a copy on the engine channel 16 above its own (copies share the decoded files with their track, a drumpad's copy plays the same pad files), so a recording's drums play on the copy of the drum track they were played on. So your live tracks stay free: play over your own take, on the same instruments, and record that as the next take. A copy gets the recorded track's volume, pan, mute / solo, hold and effects too.

- `key`, `track N select` and `master` lines are there to read; everything else is played.
- The recording's tracks are matched to this program's by kind and file, not only by number: with the tracks in another order the parts still find their instrument; a part with no matching track (a song that is not loaded) is reported and skipped.
- Sample-accurate: the audio block is split at every event, so `0.500250 pad 1 hit` sounds from frame 24012 at 48 kHz, not from the nearest block.
- You can write or edit a recording by hand. The key label may be left out (`0.5 pad 1 hit`), lines do not have to be in order, `# ...` is a comment.
- From code: `drumpad_set_observer(&pads, fn, user)` reports every change as an `AT_PadEvent`, `drumpad_event_format` / `drumpad_event_parse` turn it into text and back, `drumpad_event_apply` replays one, `drumpad_snapshot` gives the current state as events.

**How the timing works.** The replay sends every line 250 ms before it is due, from the control thread, with its exact engine frame (`engine_at`); the engine keeps such commands waiting and splits the audio block at their frame. LFOs that run on the engine clock (the strings layer's tremolo, a track's tremolo) run on a copy as they did in the take: the recording stores its start frame (`clock`) and the copy's channel clock is shifted by that much (`engine_set_channel_clock`).

**Checked** with a test program that drives the app's own key handling, one take on every kind of track: a song started and pitched up on a player track, a drum hit and a drum loop, two piano notes with a waveform change between them, the layered keyboard with a layer switched off, its track volume raised and a three-note chord, the drum loop stopped. Recorded with `F7`, loaded into the replay by itself, played back on the copies while `F6` recorded it: the replay's WAV equals the take's WAV to within 1 of 32768 over the whole take (before the channel clock shift, the strings tremolo made it 7). Earlier drum-only take: also within 1. A hand-written file with events at 0.500250 s and 1.000500 s (off the audio block grid) started its sounds on frames 24012 and 48024 exactly, and its `pitch +12` gave 880 Hz.

**Limits.** A logged time is when the key arrived, so it can be up to one device period earlier than the sound changed (10 ms in WASAPI shared mode, 3 ms exclusive, the WinMM period on WinMM); the replay plays it exactly at the logged time. Notes held down when the recording starts are not in it; a song already playing is (from its position), drum loops already running restart from their beginning. The master volume and master EQ are logged but not replayed (they belong to everything that plays now). A recording's track is found by its kind (and a player's file) and order: the first piano of the recording plays on the first piano here, and so on. If the control thread stalls longer than 250 ms (a window being dragged), lines arrive late and play at once. Not tried live in the console; the test drives the same key handler.

### Two backends, one engine

A backend is the device and nothing else. Both implement the same contract (`backend/audio_backend.h`): find a device, open a stream, then hand out the device buffer once per period (`backend_wait → backend_acquire → fill → backend_release`). Everything above that line is shared, so both produce the same sound; offline renders from the two builds are byte-identical.

| | WASAPI | WinMM |
|---|---|---|
| Runs on | Windows Vista and later | every Windows since 95 |
| Device format | float32 (shared) or whatever the hardware takes (exclusive) | 16-bit stereo |
| Default rate | the device's mix rate (48000 here) | 44100 |
| Latency | ~20 ms shared, 3 ms exclusive (measured here) | the queue: 120 ms by default |
| Wake-up | device event, once per period | waveOut event, once per finished buffer |
| Settings | `--exclusive`, `--period-us` | `--queue-ms` (latency), `--period-us` (buffer size) |

`--backend auto` (the default) uses WASAPI where it exists and WinMM otherwise. It finds out by trying to create the WASAPI device enumerator, not by reading a version number. If WASAPI exists but fails to open, auto falls back to WinMM on the default device.

In your own program:

```c
AT_Backend       backend;
AT_BackendConfig cfg = { .device_index = -1 };                 // default device, default settings
if (!backend_select_open(AT_BACKEND_AUTO, &cfg, &backend))
    puts(backend.info->error);

engine_init(&engine, (float)backend.info->sample_rate);        // the backend decides the rate
audio_thread_start(&thread, &backend, engine_render, &engine);
backend_start(&backend);
```

**WinMM queue length.** How short the queue may be depends on the machine. Measured here: the onboard Realtek chip was steady from 60 ms, the USB headset took audio in gulps of up to 70 ms and ran dry below 80 ms. The status line's `xruns` counts those dropouts (`info.underruns`); if it rises, raise `--queue-ms`.

### Windows 98

What is in place and checked on this (Windows 11) machine:

- `build.bat winmm` builds without any WASAPI code. That executable loads only `WINMM.dll`, `USER32.dll` and `KERNEL32.dll`.
- The engine's own code in that build calls 35 Windows functions, all of them present in Windows 98, all in their ANSI form (`ReadConsoleInputA`, `CreateEventA`, `waveOutGetDevCapsA`, …). The W variants are empty stubs on Windows 95/98/Me.
- Real-time priority comes from `avrt.dll` if it can be loaded at run time, otherwise from `SetThreadPriority`. The program no longer needs that DLL to start.
- A 32-bit build with SSE switched off (`/arch:IA32`) contains no SSE instructions, so it would run on Pentium- and K6-class CPUs.
- The WAV and MP3 loading is plain C (the MP3 decoder is built with its SIMD code switched off) and compiles in the GCC build for Windows 98 (`build_win98.bat`) without warnings. Rendering a file through that build gave the same audio as the Visual Studio build, to within 2 of 32768 steps (about −84 dB) including the MP3 decoding, the pitch shifter and the echo.
- The echo needs about 1 MB of memory and each player about 64 KB for its pitch shifter, on top of the decoded files.

What is still open:

- **It has never run on Windows 98.** Test it on a real machine or a VM before relying on it.
- **You need a different compiler for it.** Executables from this Visual Studio do not start on Windows 98: the file header asks for Windows Vista and the C runtime needs newer still. The source is C99/C11 (designated initializers, compound literals, `_Static_assert`), which Visual C++ 6 and 2005 cannot compile either. That leaves a GCC-based toolchain that still targets Windows 9x (classic MinGW). Untested starting point:
  ```
  gcc -std=gnu11 -O2 -march=pentium -DAT_NO_WASAPI -Isrc <every .c under src except backend/wasapi> -o audiatron.exe -lwinmm -luser32
  ```
- **Speed on old CPUs is unmeasured.** The synth computes every sample in floating point; start with `--rate 22050 --queue-ms 200` on slow machines.
- `../WINMM/winmm.h` is unchanged. This engine now plays WAV and MP3 files through `sampler/`; the SIMD sample converters of `winmm.h` are not used (the new WAV reader is plain C).

---

## 3. How to add things

### A new effect (e.g. `delay`)

1. Copy `src/effects/_template/` to `src/effects/delay/`, rename the two files to `delay.h` / `delay.c`.
2. Rename `AT_TemplateEffect` / `template_effect_*` to `AT_Delay` / `delay_*`.
3. Put parameters and memory into the struct, the DSP into `delay_process()`.
4. In `src/app/app_effects.c`: include the header, write a setup function, then add a row in `app_effects_setup`:
   ```c
   static void delay_setup(void* state, float sample_rate) { delay_init((AT_Delay*)state, sample_rate, 0.25f); }
   ...
   add(app, "delay", AT_SC_F2, 3, delay_process, sizeof(AT_Delay), delay_setup);    // slot 3 = after the echo
   ```
   That gives it a hotkey that switches it per track, a status line label and `--fx delay`. Each track gets its own instance on first use.
5. `build.bat`. Test without a device: `audiatron --offline test.wav 3 --fx delay`.

### A new instrument (e.g. `guitar`)

1. Copy `src/instruments/_template/` to `src/instruments/guitar/`, rename the files and the `template_instrument_*` names. The template already is a working 4-pad instrument.
2. Fill in the key table (playing) and `attach` (sound).
3. In `src/app/app_instruments.c`: include the header, add `static AT_Guitar s_guitar;`, then:
   ```c
   guitar_init(&s_guitar, sample_rate);
   track_list_add(&app->tracks, guitar_instrument(&s_guitar));
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
layer_stack_set_range(&stack, 1, 60, 127);                            // "high" only plays from C4 up (a split)
layer_stack_set_insert(&stack, 1, 0, tremolo_process, &tremolo);     // effect on "high" only
engine_set_source(engine, channel, &AT_LAYER_STACK_SOURCE, &stack);
```

While it plays, change a layer with `engine_set_param(engine, layer_stack_set_param, &stack, AT_LAYER_PARAM(1, AT_LAYER_PARAM_MUTE), 1.0f)`.

### A new sound generator

Create a folder (like `src/sampler/`), write the DSP, then one adapter file like `synth/synth_source.c` that fills an `AT_SourceType`. Any instrument can then own it instead of a synth or a sampler. The engine needs no change.

### A sampler-based instrument

Copy `instruments/sample_keyboard/` (it is the smallest sampler instrument): it owns an `AT_Clip` and an `AT_Sampler`, plugs the sampler in with `engine_set_source(engine, channel, &AT_SAMPLER_SOURCE, &sampler)`, and turns key events into `engine_note_on` / `engine_note_off`. For several sounds in one instrument, see `instruments/drumpad/`: one sampler per sound, each a layer of one stack with a one-note key range.

### A new file format (OGG, FLAC, ...)

Write `io/<format>_reader/` with one function of the shape `bool x_load(const char* path, AT_Clip* out, char* error, size_t cap)` (see `io/wav_reader/wav_reader.h`), then add it to `io/audio_file/audio_file.c`. Every `--play` and `--sample` file, and everything built on `audio_file_load`, then understands the format.

### A new backend (e.g. DirectSound)

Create `src/backend/dsound/`, implement the functions of `AT_Backend` (copy the shape of `backend/winmm/`: one adapter file, one for devices, one for the stream), and add it to the list in `backend/select/backend_select.c`. The audio thread and everything above it need no change.

### Changing only how the piano sounds

Edit `instruments/keyboard_piano/keyboard_piano_patch.c`. Key handling is in `keyboard_piano_keys.c` and stays untouched.

### Rule for file names

`.c` file names must be unique across all folders (all objects go to `build\obj\`). `build.bat` stops with a clear message if two collide.

---

## 4. Debugging

| Symptom | Look here first |
|---|---|
| No sound at all | `audiatron --info` (shows which backend opened), then that backend's `…_stream.c` |
| Works with one backend only | `audiatron --backend wasapi` / `--backend winmm` to compare; the engine above them is identical |
| Format / exclusive mode refused | `backend/wasapi/wasapi_format.c` |
| Crackling on WinMM, `xruns` counting up | raise `--queue-ms`; see "WinMM queue length" above |
| Crackling on WASAPI, `xruns` counting up | `threads/audio/audio_thread_loop.c`; then check the audio-thread rules below in whatever you added last |
| A key does the wrong thing | `app/app_keys.c` (global and track keys), `instruments/<name>/…_keys.c`, `input/piano_keys/piano_keys.c` (note layout) |
| Notes hang or get cut off | is the track on hold (`hold` in the status line)? Then `synth/synth_voices.c`, `dsp/envelope/adsr.c` |
| A track is silent | `mute` / `solo` in the status line, then `tracks/track_list.c` (`track_is_audible`) |
| A layer is missing, too loud or out of tune | the table in `instruments/layered_keyboard/layered_keyboard_patch.c`, then `layers/layer_stack.c` |
| A WAV / MP3 will not load | the message after `cannot load "…"`; the reader is `io/wav_reader` or `io/mp3_reader`, the dispatch `io/audio_file` |
| A recording is missing events | they are written by `app/app_record.c` (keys, app changes) and the drumpad's observer (`instruments/drumpad/drumpad_events.c`); every `drumpad_*` call reports one event |
| A replay sounds different from the take | `instruments/replay/replay_render.c` (events on frames), `instruments/replay/replay.c` (which lines are loaded) |
| A WAV recording has gaps (`some audio was lost`) | the control thread fell more than ~2.7 s behind; `recording/wav_recorder/` (`AT_WAV_RECORDER_FRAMES`) |
| A pad plays the wrong sound, or two pads at once | the key ranges set in `instruments/drumpad/drumpad.c` (`build_stack`), then `layers/layer_stack.c` (`in_range`) |
| A pad key or `+` / `-` does nothing | is the drumpad track selected (`Tab`)? Then `instruments/drumpad/drumpad_keys.c` (`k_pad_keys`, `control_of`) |
| An effect acts on more than one track or pad | it cannot by design: per track in `app/app_effects.c` (`state[track]`), per pad in `instruments/drumpad/drumpad_effects.c`. Check which track is selected and which pads are held |
| Synced loops drift apart | a synced loop pitched in tape mode leaves the clock; use keep tempo (`P`). The clock is `instruments/drumpad/drumpad_clock.c` |
| A loop will not start or stop | `instruments/drumpad/drumpad_control.c` (`pad_command`); a pad's loop is its sampler's transport voice |
| A file plays at the wrong pitch or speed | `sampler/sampler.c` (`render_voice`: rate = engine-rate conversion × `rate` × note ratio); the player's rule is in `instruments/sample_player/sample_player_control.c` |
| Fluttering sound in keep-tempo mode | the shifter (`effects/pitch_shift`); try tape mode (`T`); see "Known limit" above |
| Clicks when starting / stopping / pausing a file | the 5 ms fades in `sampler/sampler.c` (`FADE_SECONDS`) |
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
- [x] Backend contract with two implementations, WASAPI and WinMM, chosen at run time; WinMM-only build
- [x] WAV and MP3 loading, sampler (pitch, loop, transport), echo, pitch shifter, player and sampler-keyboard instruments
- [x] Drumpad: 16 pads, one file each, per-pad pitch / loop / volume / pan / pitch mode; key ranges per layer
- [x] Per-track and per-pad effects, shared clock (synced / free pads), part of a file (cut)
- [x] Recording: input as text (F5), WAV (F6), both (F7), every track; replay on copies of the tracks, sample-accurate
- [x] `engine/`: sample-accurate timed commands (`engine_at`, `engine_cancel`), observer, per-channel clock; 24 channels (12 tracks + 12 copies); a sync clock for the tracks switched to sync

**Next** (each is one new folder)
- [ ] `dsp/lfo/`, `dsp/svf/`: LFO and state-variable filter as building blocks
- [ ] `synth/`: second oscillator, per-voice filter, exponential envelopes
- [ ] `layers/`: velocity ranges per layer (key ranges are done)
- [ ] `tracks/`: a sequencer or looper, so tracks play without the keyboard
- [ ] `effects/delay/`, `effects/reverb/`, `effects/chorus/`, `effects/compressor/`
- [ ] `sampler/`: stream long files from disk instead of decoding them completely (memory on small machines)
- [ ] `effects/pitch_shift/`: a phase-aligned or phase-vocoder shifter, to remove the flutter on steady tones
- [ ] `app/`: keys for the echo and pitch-shift parameters (today only on / off, values in `app_effects.c`)
- [ ] `instruments/sample_player/`: a playlist (several files on one track)
- [ ] `io/`: OGG and FLAC readers
- [ ] `instruments/guitar/`
- [ ] `instruments/drumpad/`: load a whole folder as a kit, choke groups (open / closed hi-hat), velocity
- [ ] `equalizer/`: coefficient smoothing; frequency response from `biquad_design()` for a graphical EQ
- [ ] `engine/`: send/return busses
- [ ] `input/midi/`: `midiInOpen` → `engine_note_on`
- [ ] `backend/wasapi/`: `IMMNotificationClient` (hot-plug), capture (`IAudioCaptureClient`)
- [ ] Windows 98: build with a GCC toolchain that targets it, run on a real machine, measure CPU load

---

## 7. From winmm.h to this engine

| winmm.h | Here | Why it is better |
|---|---|---|
| `audio_init()` + `audio_pump()` every 3 ms | `backend_select_open()` + `audio_thread_start()` | The device wakes the thread by event on both backends, no polling. MMCSS priority where Windows has it |
| 3 × 2205 frames ≈ 150 ms latency | WASAPI: ~20 ms shared, 3 ms exclusive. WinMM backend: 120 ms, adjustable | Direct hardware buffer on WASAPI; a measured, tunable queue on WinMM |
| `short` interleaved, clamp in every `mix_stereo` | `float` planar, converted once in `io/sample_format` | No clipping between stages, headroom for EQ |
| `CRITICAL_SECTION` in the mixer | `core/at_queue` (lock-free) | The UI can never block the audio thread |
| `play_tone(freq, gain)` | `engine_note_on(e, ch, note, vel)` | Voice management, ADSR, retrigger instead of stacking |
| phase lock via `g_mix_clock` | `AT_Buffer.frame_clock` in every effect and source | Absolute sample time available everywhere |
| `angle_of_tone()` | `engine_set_channel_pan()` | Per channel, smoothed |
| `reverb_tone()` | an effect folder + a hook point | Any effect, anywhere in the chain |
| `set_amp_tone()` | `engine_set_channel_volume()` / velocity | Smoothed, no zipper noise |
| `play_*_WAV()`, `set_pitch_WAV()`, `reverb_tone()` on WAVs | `--play` / `--sample` (`sampler/`), pitch via the sampler rate or the pitch shifter, echo as an effect | WAV and MP3, one loader, 8 voices, cubic interpolation (was nearest / linear), click-free fades |

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
