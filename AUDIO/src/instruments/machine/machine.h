// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine.h — mechanical sounds driven by a throttle
//
//  A machine (a petrol engine, a generator, a drill, a fan ...) is one sound
//  file played as an endless loop over a part of the file. A THROTTLE between
//  0 (idle) and 1 (full) decides, through four curves, how it sounds:
//
//      gain    how loud                          (linear gain)
//      from    where the looped part starts      (seconds into the file)
//      to      where the looped part ends        (seconds into the file)
//      rate    how fast the file is read         (1 = original speed and pitch)
//
//  The machine also has its own EQ, crush, tremolo and echo (each off until
//  switched on), and every one of their settings is a curve on the same
//  throttle: the muffler opens (EQ low-pass), the tremolo speeds up, the echo
//  gets shorter ... They follow the smoothed throttle every 32 frames.
//
//  Default, as asked for: idle loops 2.0..4.0 s quietly, full throttle loops
//  2.0..3.0 s loudly; the rate stays 1. Every curve has an idle value, a full
//  value and a SHAPE that says how it gets from one to the other:
//
//      linear     straight line                       f(t) = t
//      ease in    slow at first, fast at the end      f(t) = t^amount
//      ease out   fast at first, slow at the end      f(t) = 1 - (1-t)^amount
//      s-curve    slow at both ends                   smoothstep
//      custom     through three points of your own at t = 0.25, 0.5, 0.75
//
//      value = idle + (full - idle) * f(throttle)
//
//  Playing it (keys on the machine's track):
//      W  hold: throttle up (starts the machine if it is off)
//      S  hold: throttle down
//      Q  machine on / off          R  throttle back to idle
//      X  auto idle on / off: with no key held the throttle sinks back to idle
//      G  gearbox: off -> manual -> automatic -> off
//      A / D  shift down / up (manual; in automatic a request it may undo)
//      1 2 3 4  built-in sound only (your setup stays): petrol engine, jet, propeller, rocket
//      5 6 7 8  factory setup: that sound and the curves, effects and gearbox that suit it
//      P        save the setup as machines\machine_NNN.atengine
//
//  ENGINE FILES (machine_file.c, .atengine): the sound and the whole setup as
//  text lines, to keep a library of engines and load them again, also with
//  --machine my_engine.atengine.
//
//  BUILT-IN SOUNDS (machine_builtin.c): a petrol engine, a jet (turbofan), a
//  propeller aircraft and a rocket, synthesized at start, each with curves,
//  effects and gearbox settings that suit it (machine_preset).
//
//  THE GEARBOX (machine_gearbox.c). Off, the curves read the throttle. On, the
//  throttle is the gas pedal of a vehicle and the curves read the ENGINE SPEED
//  (rev: 0 at idle rpm, 1 at the rev limit), which has momentum:
//
//      neutral / clutch open   the engine revs freely toward the pedal's rpm,
//                              rising with rev_up_s, falling with rev_down_s
//      in gear                 rpm = speed x the gear's ratio; the pedal
//                              accelerates the vehicle, drag and engine braking
//                              slow it; below idle the clutch slips (pulling away)
//      a shift                 clutch out (the pedal lifted by `lift`), gear
//                              change, clutch in: the rpm moves to the new gear's
//                              along that gear's MOMENTUM CURVE (a shape over the
//                              clutch-in time), so every shift can sound its own way
//      automatic               up at the gear's up_rpm x the shift-point curve
//                              over the pedal (light foot: early, full pedal: at the
//                              limit), down below down_rpm, kickdown at full pedal
//
//  The resulting rev goes to the sound as the throttle parameter, so a
//  recording has the engine as it sounded and the replay needs no gearbox.
//
//  Sound: the loop seam is crossfaded (equal power), and when the throttle
//  moves the loop's edges under the read position, the jump is crossfaded too,
//  so changing the throttle never clicks. The throttle itself is smoothed on
//  the audio thread.
//
//  Files in this folder:
//    machine.c          settings, curves, loading, the built-in engine sound, the instrument
//    machine_source.c   the sound: crossfaded loop reader      (audio thread)
//    machine_keys.c     the keys and the throttle movement     (control thread)
//    machine_builtin.c  the built-in sounds and their presets
//    machine_gearbox.c  gears, clutch, engine momentum         (control thread)
//    machine_file.c     the setup as text: .atengine files
//
//  Threading: like every instrument, settings are written directly before the
//  machine is attached and go through engine_set_param afterwards; use the
//  machine_set_* functions, which do the right thing either way.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <stdio.h>

#include "dsp/biquad/biquad.h"
#include "effects/bitcrusher/bitcrusher.h"
#include "effects/echo/echo.h"
#include "instruments/instrument.h"
#include "io/audio_clip/audio_clip.h"

typedef enum {
    AT_MACHINE_CURVE_GAIN,
    AT_MACHINE_CURVE_FROM,
    AT_MACHINE_CURVE_TO,
    AT_MACHINE_CURVE_RATE,
    // the machine's own effects, also on the throttle (each only while its effect is on)
    AT_MACHINE_CURVE_CRUSH_DOWNSAMPLE,      // hold every Nth sample, 1..64
    AT_MACHINE_CURVE_CRUSH_BITS,            // 1..16
    AT_MACHINE_CURVE_TREMOLO_RATE,          // Hz
    AT_MACHINE_CURVE_TREMOLO_DEPTH,         // 0..1
    AT_MACHINE_CURVE_ECHO_TIME,             // ms
    AT_MACHINE_CURVE_ECHO_FEEDBACK,         // 0..0.95
    AT_MACHINE_CURVE_ECHO_MIX,              // 0..1
    AT_MACHINE_CURVE_EQ_LOW,                // dB, low shelf at 120 Hz
    AT_MACHINE_CURVE_EQ_MID_FREQ,           // Hz, the bell's frequency
    AT_MACHINE_CURVE_EQ_MID_GAIN,           // dB
    AT_MACHINE_CURVE_EQ_HIGH,               // dB, high shelf at 6 kHz
    AT_MACHINE_CURVE_EQ_LOWPASS,            // Hz: the muffler, it opens as the throttle rises
    AT_MACHINE_CURVES
} AT_MachineCurveId;

// The machine's own effects, after the loop reader, in this order. Off by default.
typedef enum {
    AT_MACHINE_FX_EQ,
    AT_MACHINE_FX_CRUSH,
    AT_MACHINE_FX_TREMOLO,
    AT_MACHINE_FX_ECHO,
    AT_MACHINE_FX_COUNT
} AT_MachineFx;

typedef enum {
    AT_MACHINE_SHAPE_LINEAR,
    AT_MACHINE_SHAPE_EASE_IN,
    AT_MACHINE_SHAPE_EASE_OUT,
    AT_MACHINE_SHAPE_S_CURVE,
    AT_MACHINE_SHAPE_CUSTOM,
    AT_MACHINE_SHAPES
} AT_MachineShape;

#define AT_MACHINE_POINTS 3                 // custom shape: f at t = 0.25, 0.5, 0.75 (f(0) = 0, f(1) = 1)

typedef struct {
    float           idle;                   // value at throttle 0
    float           full;                   // value at throttle 1
    AT_MachineShape shape;
    float           amount;                 // ease in / out: the exponent, 1..6
    float           points[AT_MACHINE_POINTS];
} AT_MachineCurve;

// Parameters (AT_ParamFn machine_set_param, self = &machine->sound).
typedef enum {
    AT_MACHINE_PARAM_THROTTLE,              // 0..1
    AT_MACHINE_PARAM_RUNNING,               // 1 = on (fades in), 0 = off (fades out)
    AT_MACHINE_PARAM_VOLUME,                // linear, after the gain curve
    AT_MACHINE_PARAM_SMOOTH_MS,             // how fast the sound follows the throttle
    AT_MACHINE_PARAM_XFADE_MS,              // crossfade at the loop seam
    AT_MACHINE_PARAM_CLIP,                  // which of the two clip slots plays (machine_load sends it)
    AT_MACHINE_PARAM_FX_EQ,                 // 1 / 0: the machine's effects on / off (AT_MACHINE_PARAM_FX_EQ + AT_MachineFx)
    AT_MACHINE_PARAM_FX_CRUSH,
    AT_MACHINE_PARAM_FX_TREMOLO,
    AT_MACHINE_PARAM_FX_ECHO,
    AT_MACHINE_PARAM_RESTART,               // any value: the sound starts over from its loop's start where the command
                                            // lands (a song starting, its loop jumping back): the running sound fades
                                            // out, its tremolo from the top
    AT_MACHINE_PARAM_CURVE_BASE = 16
} AT_MachineParam;

typedef enum {
    AT_MACHINE_FIELD_IDLE,
    AT_MACHINE_FIELD_FULL,
    AT_MACHINE_FIELD_SHAPE,
    AT_MACHINE_FIELD_AMOUNT,
    AT_MACHINE_FIELD_POINT1,
    AT_MACHINE_FIELD_POINT2,
    AT_MACHINE_FIELD_POINT3,
    AT_MACHINE_FIELDS
} AT_MachineField;

#define AT_MACHINE_FIELD_STRIDE 8
#define AT_MACHINE_PARAM_CURVE(curve, field) \
    ((int)AT_MACHINE_PARAM_CURVE_BASE + (int)(curve) * AT_MACHINE_FIELD_STRIDE + (int)(field))
#define AT_MACHINE_PARAM_COUNT AT_MACHINE_PARAM_CURVE(AT_MACHINE_CURVES, 0)

#define AT_MACHINE_MIN_LOOP_S 0.02f         // the looped part is never shorter than this

// The sound the engine renders. Audio thread only once plugged in.
typedef struct {
    AT_Engine*      engine;                 // for the sync clock (set at attach)
    uint32_t        channel;
    const AT_Clip*  slots;                  // the machine's two clip slots (not owned)
    int             slot;                   // the one that plays
    float           sample_rate;

    bool            running;
    float           run_env;                // 0..1 fade for on / off
    float           throttle_target;
    float           throttle;               // smoothed
    float           smooth_ms;
    float           xfade_ms;
    float           volume;
    AT_MachineCurve curves[AT_MACHINE_CURVES];

    double          head[2];                // read positions in clip frames: [0] plays, [1] fades out
    uint32_t        jump_left;              // output frames of the running crossfade
    uint32_t        jump_len;
    float           gain;                   // the gain curve's value, ramped per sample
    bool            started;                // head[0] has been placed in the loop
    bool            restart;                // AT_MACHINE_PARAM_RESTART: from the loop's start on the next frames
    // The pieces the sound is worked out in (throttle, gain, rate, loop per piece) keep their own grid across render
    // calls, started where the sound starts: wherever the engine's blocks and commands cut the calls, each pass of a
    // song's loop is then cut alike and sounds alike.
    uint32_t        piece_left;             // frames left in the current piece; 0: a new one
    float           piece_t, piece_g0;      // its throttle (at its end) and its gain at its start (s->gain at its end)
    double          piece_from, piece_to, piece_rate, piece_xfade;

    // the machine's own effects
    bool            fx_on[AT_MACHINE_FX_COUNT];
    AT_BiquadState  eq_state[AT_MAX_CHANNELS][4];   // low shelf, bell, high shelf, low-pass
    AT_Bitcrusher   crush;
    double          tremolo_phase;          // its own phase: the rate follows the throttle without jumps
    AT_Echo*        echo;                   // NULL until echo is first switched on (~1 MB, the control thread owns it)
    uint32_t        tail_left;              // frames the effects keep running after the machine stopped (echo tail)

    // published for meters and displays (approximate)
    volatile float  shown_throttle;
    volatile float  shown_head_s;           // where the playing head is, seconds into the file
    volatile float  shown_from_s;
    volatile float  shown_to_s;
    volatile int    shown_running;
} AT_MachineSound;

// ─── Built-in sounds ─────────────────────────────────────────────────────────

typedef enum {
    AT_MACHINE_BUILTIN_PETROL,              // four-cylinder car engine
    AT_MACHINE_BUILTIN_JET,                 // turbofan: roar, fan whine
    AT_MACHINE_BUILTIN_PROPELLER,           // piston aircraft: blade pass, exhaust
    AT_MACHINE_BUILTIN_ROCKET,              // rocket motor: roar and crackle
    AT_MACHINE_BUILTINS
} AT_MachineBuiltin;

// ─── Gearbox ─────────────────────────────────────────────────────────────────

#define AT_MACHINE_GEARS 8                  // forward gears at most

typedef enum {
    AT_MACHINE_GEARBOX_OFF,
    AT_MACHINE_GEARBOX_MANUAL,
    AT_MACHINE_GEARBOX_AUTOMATIC
} AT_MachineGearboxMode;

// One gear, and how a shift INTO it goes.
typedef struct {
    float           ratio;                  // engine rpm per km/h
    float           up_rpm;                 // automatic: shift up here at full pedal (the shift-point curve scales it)
    float           down_rpm;               // automatic: shift down below this
    float           clutch_out_ms;          // the clutch opens
    float           shift_ms;               // the gear changes, the clutch open: the engine runs free
    float           clutch_in_ms;           // the clutch closes: the rpm moves to this gear's
    float           lift;                   // 0..1: how far the pedal is lifted during the shift
    AT_MachineCurve momentum;               // the rpm's path while the clutch closes (idle = 0 / full = 1:
                                            // only its shape counts): ease out = quick then settling
} AT_MachineGear;

typedef struct {
    AT_MachineGearboxMode mode;
    int             gears;                  // forward gears, 1..AT_MACHINE_GEARS
    float           idle_rpm;
    float           max_rpm;                // the rev limiter
    float           rev_up_s;               // engine momentum, free: seconds to get most of the way up ...
    float           rev_down_s;             // ... and down
    float           power;                  // km/h per second at full pedal in first gear
    float           drag;                   // slowing at 100 km/h, km/h per second (grows with speed squared)
    float           engine_brake;           // slowing with the pedal up at the rev limit, km/h per second
    AT_MachineCurve shift_point;            // automatic: up_rpm x this curve's value at the pedal
    AT_MachineGear  gear[AT_MACHINE_GEARS];
} AT_MachineGearbox;

// What the gearbox is doing now (control thread).
typedef struct {
    int             gear;                   // 0 = neutral
    int             target;                 // the gear a shift goes into
    int             phase;                  // 0 driving, 1 clutch out, 2 shifting, 3 clutch in
    float           phase_s;                // seconds into the phase
    float           rpm;
    float           free_rpm;               // where the engine would be with the clutch open
    float           speed;                  // km/h
    float           clutch;                 // 1 closed .. 0 open
    float           rpm_from;               // rpm when the clutch began to close
    float           rev;                    // 0..1, what the sound gets
    int             shifts;                 // shifts so far
} AT_MachineDrive;

typedef struct {
    AT_Clip          clips[2];              // the sound file; a new file is loaded into the slot that is not playing
    bool             owns_clips;            // false for the replay's copy
    int              slot;                  // control view: the slot the sound was told to play
    uint64_t         slot_frame;            // engine frame when that was sent
    AT_MachineSound  sound;
    AT_Engine*       engine;                // NULL until attach
    uint32_t         channel;

    // control view of the settings (what is shown; changed with machine_set_*)
    AT_MachineCurve  curves[AT_MACHINE_CURVES];
    float            throttle;
    bool             running;
    float            volume;
    float            smooth_ms;
    float            xfade_ms;

    // the throttle's movement while W / S are held (per second of holding)
    float            rise_per_s;
    float            fall_per_s;
    float            idle_per_s;            // with auto idle and no key held
    bool             auto_idle;
    bool             driven;                // a sequencer drives the throttle (an arrangement's clip): auto idle leaves it alone
    bool             key_up, key_down;
    uint64_t         last_tick;             // engine frame of the last tick
    uint64_t         running_frame;         // engine frame when on / off was last sent
    void*            copy;                  // the replay's copy (AT_Machine*): it shares the file, so it switches with it

    bool             fx[AT_MACHINE_FX_COUNT];   // control view: which of its effects are on
    void*            echo_memory;           // the echo, allocated the first time it is switched on

    AT_MachineGearbox gearbox;
    AT_MachineDrive  drive;
    bool             shift_up, shift_down;  // A / D pressed since the last tick
    float            sent;                  // the last value the sound got as its throttle
    AT_MachineBuiltin builtin;              // which built-in sound plays (when path is "")

    char             label[64];             // file name, or "built-in engine"
    char             path[260];             // the file, "" = built-in
    float            sample_rate;
} AT_Machine;

// Sets everything to the defaults and makes the built-in engine sound, so the
// machine plays without any file.
bool machine_init(AT_Machine* m, float sample_rate);
// A WAV / MP3 to use instead. Works before and after attach (then it switches
// to the new file between two audio blocks). false: see `error`, nothing changed.
bool machine_load(AT_Machine* m, const char* path, char* error, size_t error_cap);
bool machine_load_builtin(AT_Machine* m, AT_MachineBuiltin kind);    // machine_builtin.c
// The curves, effects, follow time and gearbox that suit a built-in sound.
void machine_preset      (AT_Machine* m, AT_MachineBuiltin kind);
// "petrol", "jet", "propeller", "rocket" -> kind. false = not a built-in name.
bool machine_find_builtin(const char* name, AT_MachineBuiltin* out);
extern const char* const AT_MACHINE_BUILTIN_NAMES[AT_MACHINE_BUILTINS];
extern const char* const AT_MACHINE_BUILTIN_LABELS[AT_MACHINE_BUILTINS];
void machine_free(AT_Machine* m);
// The replay's copy: plays the same file (shared), same settings.
void machine_init_copy(AT_Machine* m, AT_Machine* original, float sample_rate);

void machine_set_throttle (AT_Machine* m, float throttle);
void machine_set_running  (AT_Machine* m, bool running);
void machine_set_volume   (AT_Machine* m, float volume);
void machine_set_smooth_ms(AT_Machine* m, float ms);
void machine_set_xfade_ms (AT_Machine* m, float ms);
void machine_set_curve    (AT_Machine* m, AT_MachineCurveId curve, const AT_MachineCurve* c);
void machine_reset_curves (AT_Machine* m);  // back to the defaults (2-4 s idle, 2-3 s full)
// Switches one of the machine's own effects. false: no memory for the echo (it stays off).
bool machine_set_fx       (AT_Machine* m, AT_MachineFx fx, bool on);

// The shape between 0 and 1, and a curve's value at a throttle.
float machine_shape_eval(const AT_MachineCurve* c, float t);
float machine_curve_eval(const AT_MachineCurve* c, float t);
double machine_seconds(const AT_Machine* m);                    // length of the file that plays
const AT_Clip* machine_clip(const AT_Machine* m);               // the file that plays (control view)

AT_Instrument machine_instrument(AT_Machine* m);

// AT_ParamFn — self = AT_MachineSound*, param = AT_MachineParam or AT_MACHINE_PARAM_CURVE(...)
void machine_set_param(void* self, int param, float value);
extern const AT_SourceType AT_MACHINE_SOURCE;
extern const char* const AT_MACHINE_CURVE_NAMES[AT_MACHINE_CURVES];   // "gain", "from", "to", "rate", "crush_bits", ...
extern const char* const AT_MACHINE_FX_NAMES[AT_MACHINE_FX_COUNT];     // "eq", "crush", "tremolo", "echo"
extern const char* const AT_MACHINE_SHAPE_NAMES[AT_MACHINE_SHAPES];

// Inside this folder: the clip of a built-in sound (machine_builtin.c), and the value
// the curves read sent to the sound (machine.c; force = even if it did not change).
bool machine_builtin_render(AT_MachineBuiltin kind, AT_Clip* clip);
void machine_send_drive(AT_Machine* m, float value, bool force);

// machine_file.c — what is written and read
typedef enum {
    AT_MACHINE_SAVE_SOUND   = 1,            // the sound line (built-in name or file)
    AT_MACHINE_SAVE_CURVES  = 2,            // all curves
    AT_MACHINE_SAVE_FX      = 4,            // which of its effects are on
    AT_MACHINE_SAVE_MOTION  = 8,            // volume, follow, crossfade, rise / fall / idle rate, auto idle
    AT_MACHINE_SAVE_GEARBOX = 16,           // the gearbox and every gear
    AT_MACHINE_SAVE_ALL     = 31,
    AT_MACHINE_SAVE_SETUP   = 30            // everything but the sound
} AT_MachineSaveParts;

void machine_write_settings(const AT_Machine* m, FILE* f, unsigned parts);
// The same lines, each handed to emit (no newline), to put them into a file of your own.
void machine_settings_snapshot(const AT_Machine* m, unsigned parts, void (*emit)(void* user, const char* line), void* user);
// One line as machine_write_settings writes it; lines of other parts or unknown lines are skipped (true).
// base_dir: where a sound file is also looked for (the preset's folder), may be NULL. false: see error.
bool machine_read_setting(AT_Machine* m, const char* line, unsigned parts, const char* base_dir, char* error, size_t error_cap);
bool machine_save_preset (const AT_Machine* m, const char* path, unsigned parts, char* error, size_t error_cap);
bool machine_load_preset (AT_Machine* m, const char* path, unsigned parts, char* error, size_t error_cap);

// machine_gearbox.c — control thread
void machine_gearbox_defaults(AT_MachineGearbox* g);                // a six-speed car
void machine_set_gearbox_mode(AT_Machine* m, AT_MachineGearboxMode mode);
void machine_shift(AT_Machine* m, int direction);                   // +1 up, -1 down (0 = neutral below 1st)
void machine_drive_step(AT_Machine* m, float dt);                   // moves the engine and the vehicle on
float machine_gearbox_shift_rpm(const AT_Machine* m, int gear);     // automatic: shift-up rpm at the pedal now
extern const char* const AT_MACHINE_GEARBOX_MODE_NAMES[3];          // "off", "manual", "automatic"

// machine_keys.c
bool machine_key        (AT_Machine* m, const AT_KeyEvent* ev);
void machine_release_all(AT_Machine* m);
void machine_tick       (AT_Machine* m);    // moves the throttle while W / S are held
extern const char MACHINE_HELP[];
