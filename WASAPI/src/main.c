// ═══════════════════════════════════════════════════════════════════════════
//  main.c — AUDIATRON WASAPI engine: console piano + hook demos
//
//  Control thread only. Everything here talks to the engine through the
//  engine_* command API; nothing here ever touches audio buffers directly —
//  except the two demo hooks, which RUN on the audio thread.
//
//  Usage:
//    audiatron                         shared mode, default device, keyboard piano
//    audiatron --exclusive             exclusive mode (lowest latency)
//    audiatron --list                  list output devices
//    audiatron --device N              use device N from --list
//    audiatron --rate 48000            request a sample rate
//    audiatron --period-us 3000        request a device period
//    audiatron --info                  open the device, print the format, exit
//    audiatron --offline out.wav [s]   render a demo to a WAV file, no device
//    audiatron --crush / --tremolo     start with that demo hook enabled
// ═══════════════════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>

#include "wasapi_backend.h"
#include "dsp_engine.h"

static AT_Engine  g_engine;         // ~45 KB — static, not on the stack
static AT_Backend g_backend;

// ─── Demo hook 1: bitcrusher (master bus, AT_HOOK_MASTER_POST) ──────────────
// Sample-rate reduction + bit-depth reduction. Shows full in-place buffer
// manipulation across both channels.

typedef struct {
    uint32_t downsample;    // hold every Nth sample
    float    levels;        // quantization steps per unit (2^(bits-1))
    uint32_t counter;
    float    held[AT_MAX_CHANNELS];
} Bitcrusher;

static void hook_bitcrusher(AT_Buffer* buf, void* user) {
    Bitcrusher* bc = (Bitcrusher*)user;
    for (uint32_t i = 0; i < buf->frames; i++) {
        bool take = (bc->counter++ % bc->downsample) == 0;
        for (uint32_t c = 0; c < buf->channels; c++) {
            if (take) bc->held[c] = roundf(buf->ch[c][i] * bc->levels) / bc->levels;
            buf->ch[c][i] = bc->held[c];
        }
    }
}

// ─── Demo hook 2: tremolo (channel 0, AT_HOOK_CHANNEL) ──────────────────────
// Uses buf->frame_clock, so the LFO is locked to absolute sample time and is
// continuous across blocks no matter how the device chunks them.

typedef struct {
    float rate_hz;
    float depth;            // 0..1
} Tremolo;

static void hook_tremolo(AT_Buffer* buf, void* user) {
    Tremolo* t = (Tremolo*)user;
    double   w = AT_TWO_PI * t->rate_hz / buf->sample_rate;
    for (uint32_t i = 0; i < buf->frames; i++) {
        double phase = w * (double)(buf->frame_clock + i);
        float  gain  = 1.0f - t->depth * 0.5f * (1.0f - (float)cos(phase));
        for (uint32_t c = 0; c < buf->channels; c++)
            buf->ch[c][i] *= gain;
    }
}

static Bitcrusher g_crusher = { .downsample = 6, .levels = 16.0f };
static Tremolo    g_tremolo = { .rate_hz = 6.0f, .depth = 0.8f };

static void set_crusher(bool on) {
    engine_set_hook(&g_engine, AT_HOOK_MASTER_POST, 0, 0, on ? hook_bitcrusher : NULL, &g_crusher);
}

static void set_tremolo(bool on) {
    engine_set_hook(&g_engine, AT_HOOK_CHANNEL, 0, 0, on ? hook_tremolo : NULL, &g_tremolo);
}

// ─── Default patch ───────────────────────────────────────────────────────────

static void load_default_patch(void) {
    engine_set_waveform(&g_engine, 0, AT_WAVE_SAW);
    engine_set_adsr    (&g_engine, 0, (AT_ADSRParams){ 0.005f, 0.25f, 0.6f, 0.4f });

    // channel 0: tame the saw's top end, cut rumble
    engine_set_eq_band(&g_engine, 0, 0, AT_FILTER_HIGHPASS, 60.0f,   0.707f, 0.0f);
    engine_set_eq_band(&g_engine, 0, 1, AT_FILTER_LOWPASS,  5000.0f, 0.707f, 0.0f);

    // master: a little warmth and air
    engine_set_eq_band(&g_engine, -1, 0, AT_FILTER_LOWSHELF,  150.0f,  0.707f,  3.0f);
    engine_set_eq_band(&g_engine, -1, 1, AT_FILTER_PEAK,      2500.0f, 1.0f,   -2.0f);
    engine_set_eq_band(&g_engine, -1, 2, AT_FILTER_HIGHSHELF, 9000.0f, 0.707f,  2.0f);

    engine_set_master_volume(&g_engine, 0.8f);
}

// ─── Console helpers ─────────────────────────────────────────────────────────

static void print_wide(const wchar_t* s) {
    char utf8[512];
    WideCharToMultiByte(CP_UTF8, 0, s, -1, utf8, sizeof utf8, NULL, NULL);
    fputs(utf8, stdout);
}

typedef struct { int wanted; wchar_t id[512]; bool found; } DevicePick;

static void on_list_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)id; (void)user;
    printf("  [%u]%s ", index, is_default ? " *" : "  ");
    print_wide(name);
    printf("\n");
}

static void on_pick_device(uint32_t index, const wchar_t* id, const wchar_t* name, bool is_default, void* user) {
    (void)name; (void)is_default;
    DevicePick* pick = (DevicePick*)user;
    if ((int)index == pick->wanted) {
        wcsncpy_s(pick->id, sizeof pick->id / sizeof *pick->id, id, _TRUNCATE);
        pick->found = true;
    }
}

static void print_stream_info(const AT_Backend* b) {
    printf("Device : ");
    print_wide(b->device_name);
    printf("\nMode   : %s\n", b->share_mode == AT_SHARE_EXCLUSIVE ? "EXCLUSIVE" : "shared");
    printf("Format : %u Hz, %u ch, %s (%u valid bits)\n",
           b->sample_rate, b->channels, backend_sample_type_name(b->sample_type), b->valid_bits);
    printf("Buffer : %u frames (%.2f ms), period %.2f ms\n",
           b->buffer_frames, 1000.0 * b->buffer_frames / b->sample_rate, b->period_ms);
}

static void meter_bar(char* out, float peak, int width) {
    float db    = at_gain_to_db(peak);
    int   filled = (int)((db + 60.0f) / 60.0f * (float)width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    for (int i = 0; i < width; i++) out[i] = i < filled ? (i >= width - 2 ? '!' : '#') : '.';
    out[width] = '\0';
}

// ─── Keyboard piano (physical key positions → layout independent) ───────────
// Scan codes, not virtual keys: works identically on QWERTZ and QWERTY.
//
//   black:   W E   T Z U   O P          (QWERTZ labels)
//   white:  A S D F G H J K L Ö
//            C D E F G A H C D E

static int scan_to_semitone(WORD sc) {
    switch (sc) {
    case 0x1E: return 0;   case 0x11: return 1;   case 0x1F: return 2;
    case 0x12: return 3;   case 0x20: return 4;   case 0x21: return 5;
    case 0x14: return 6;   case 0x22: return 7;   case 0x15: return 8;
    case 0x23: return 9;   case 0x16: return 10;  case 0x24: return 11;
    case 0x25: return 12;  case 0x18: return 13;  case 0x26: return 14;
    case 0x19: return 15;  case 0x27: return 16;
    default:   return -1;
    }
}

#define SC_ESC      0x01
#define SC_1        0x02    // 1..5 → 0x02..0x06
#define SC_SPACE    0x39
#define SC_OCT_DOWN 0x2C    // Y (QWERTZ) / Z (QWERTY)
#define SC_OCT_UP   0x2D    // X
#define SC_CRUSH    0x2E    // C
#define SC_TREMOLO  0x2F    // V
#define SC_EQ       0x30    // B
#define SC_VOL_DOWN 0x31    // N
#define SC_VOL_UP   0x32    // M

static void print_help(void) {
    printf("\nKeys (physical positions, QWERTZ labels):\n"
           "  A W S E D F T G Z H U J K O L P Oe   play notes      Y / X   octave -/+\n"
           "  1..5  waveform (sine saw square tri noise)       Space   all notes off\n"
           "  C  bitcrusher hook   V  tremolo hook   B  master EQ bypass   N/M  volume -/+\n"
           "  Esc  quit\n\n");
}

static int run_interactive(bool crush, bool tremolo) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD  old_mode;
    if (!GetConsoleMode(in, &old_mode)) {
        fprintf(stderr, "stdin is not a console — interactive mode needs a real terminal.\n");
        return 1;
    }
    SetConsoleMode(in, ENABLE_EXTENDED_FLAGS);      // raw key events, no echo, no quick-edit

    print_help();

    int   octave      = 4;
    int   wave        = AT_WAVE_SAW;
    float volume      = 0.8f;
    bool  eq_bypass   = false;
    int   held[256];                                // scan code → sounding note (or -1)
    for (int i = 0; i < 256; i++) held[i] = -1;

    bool running = true;
    while (running) {
        if (WaitForSingleObject(in, 33) == WAIT_OBJECT_0) {
            INPUT_RECORD recs[32];
            DWORD        count = 0;
            ReadConsoleInputW(in, recs, 32, &count);

            for (DWORD r = 0; r < count; r++) {
                if (recs[r].EventType != KEY_EVENT) continue;
                const KEY_EVENT_RECORD* k = &recs[r].Event.KeyEvent;
                WORD sc = k->wVirtualScanCode & 0xFF;

                int semitone = scan_to_semitone(sc);
                if (semitone >= 0) {
                    if (k->bKeyDown && held[sc] < 0) {             // ignore auto-repeat
                        held[sc] = 12 * (octave + 1) + semitone;
                        engine_note_on(&g_engine, 0, held[sc], 0.8f);
                    } else if (!k->bKeyDown && held[sc] >= 0) {
                        engine_note_off(&g_engine, 0, held[sc]);   // the note that key started
                        held[sc] = -1;
                    }
                    continue;
                }
                if (!k->bKeyDown) continue;

                switch (sc) {
                case SC_ESC:      running = false; break;
                case SC_SPACE:    engine_all_notes_off(&g_engine, -1); break;
                case SC_OCT_DOWN: if (octave > 0) octave--; break;
                case SC_OCT_UP:   if (octave < 8) octave++; break;
                case SC_CRUSH:    crush   = !crush;   set_crusher(crush);   break;
                case SC_TREMOLO:  tremolo = !tremolo; set_tremolo(tremolo); break;
                case SC_EQ:       eq_bypass = !eq_bypass; engine_set_eq_bypass(&g_engine, -1, eq_bypass); break;
                case SC_VOL_DOWN: volume = at_clampf(volume - 0.1f, 0.0f, 2.0f); engine_set_master_volume(&g_engine, volume); break;
                case SC_VOL_UP:   volume = at_clampf(volume + 0.1f, 0.0f, 2.0f); engine_set_master_volume(&g_engine, volume); break;
                default:
                    if (sc >= SC_1 && sc < SC_1 + AT_WAVE_COUNT) {
                        wave = sc - SC_1;
                        engine_set_waveform(&g_engine, 0, (AT_Waveform)wave);
                    }
                    break;
                }
            }
        }

        char l[25], r[25];
        meter_bar(l, engine_master_peak(&g_engine, 0), 24);
        meter_bar(r, engine_master_peak(&g_engine, 1), 24);
        printf("\rL[%s] R[%s] oct %d %-8s vol %.1f %s%s%s xruns %ld   ",
               l, r, octave, synth_waveform_name((AT_Waveform)wave), volume,
               crush ? "CRUSH " : "", tremolo ? "TREM " : "", eq_bypass ? "EQ-OFF " : "",
               g_backend.callback_overruns);
        fflush(stdout);

        if (g_backend.device_lost) {
            printf("\nDevice lost (unplugged or format changed). Exiting.\n");
            break;
        }
    }

    printf("\n");
    SetConsoleMode(in, old_mode);
    return 0;
}

// ─── Offline render → WAV (no device; great for testing hooks) ──────────────

static void write_u32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_u16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_wav_header(FILE* f, uint32_t rate, uint32_t frames) {
    uint32_t data_bytes = frames * 2 * sizeof(int16_t);
    fwrite("RIFF", 1, 4, f); write_u32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); write_u32(f, 16);
    write_u16(f, 1);                    // PCM
    write_u16(f, 2);                    // stereo
    write_u32(f, rate);
    write_u32(f, rate * 4);
    write_u16(f, 4);
    write_u16(f, 16);
    fwrite("data", 1, 4, f); write_u32(f, data_bytes);
}

static int run_offline(const char* path, double seconds, uint32_t rate) {
    FILE* f = NULL;
    if (fopen_s(&f, path, "wb") != 0 || !f) { fprintf(stderr, "cannot open %s\n", path); return 1; }

    const uint32_t block        = 480;
    const uint32_t total        = (uint32_t)(seconds * rate);
    const uint32_t step_frames  = rate / 4;                     // new note every 250 ms
    static const int arpeggio[] = { 48, 55, 60, 64, 67, 72, 67, 64, 60, 55 };
    const int        arp_len    = (int)(sizeof arpeggio / sizeof *arpeggio);

    static float L[AT_MAX_BLOCK_FRAMES], R[AT_MAX_BLOCK_FRAMES];
    int16_t      pcm[AT_MAX_BLOCK_FRAMES * 2];
    int          step = -1;
    float        peak = 0.0f;
    double       sum_sq = 0.0;

    write_wav_header(f, rate, 0);                               // patched at the end
    at_enable_flush_to_zero();

    for (uint32_t done = 0; done < total; ) {
        int want_step = (int)(done / step_frames);
        if (want_step != step) {
            if (step >= 0) engine_note_off(&g_engine, 0, arpeggio[step % arp_len]);
            step = want_step;
            engine_note_on(&g_engine, 0, arpeggio[step % arp_len], 0.9f);
        }

        uint32_t  n   = total - done < block ? total - done : block;
        AT_Buffer buf = { .ch = { L, R }, .channels = 2, .frames = n, .sample_rate = rate };
        engine_render(&buf, &g_engine);

        for (uint32_t i = 0; i < n; i++) {
            for (int c = 0; c < 2; c++) {
                float x = buf.ch[c][i];
                peak    = fmaxf(peak, fabsf(x));
                sum_sq += (double)x * x;
                pcm[i * 2 + c] = (int16_t)lrintf(at_clampf(x, -1.0f, 1.0f) * 32767.0f);
            }
        }
        fwrite(pcm, sizeof(int16_t), n * 2, f);
        done += n;
    }

    fseek(f, 0, SEEK_SET);
    write_wav_header(f, rate, total);
    fclose(f);

    printf("Rendered %.2f s @ %u Hz to %s — peak %.1f dBFS, RMS %.1f dBFS\n",
           seconds, rate, path, at_gain_to_db(peak),
           at_gain_to_db((float)sqrt(sum_sq / (2.0 * total))));
    return 0;
}

// ─── Entry ───────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    AT_BackendConfig cfg = { .share_mode = AT_SHARE_SHARED };
    const char* offline_path = NULL;
    double      offline_secs = 5.0;
    int         device_index = -1;
    bool        list = false, info = false, crush = false, tremolo = false;

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if      (!strcmp(a, "--exclusive"))               cfg.share_mode = AT_SHARE_EXCLUSIVE;
        else if (!strcmp(a, "--list"))                    list = true;
        else if (!strcmp(a, "--info"))                    info = true;
        else if (!strcmp(a, "--crush"))                   crush = true;
        else if (!strcmp(a, "--tremolo"))                 tremolo = true;
        else if (!strcmp(a, "--device")    && i + 1 < argc) device_index  = atoi(argv[++i]);
        else if (!strcmp(a, "--rate")      && i + 1 < argc) cfg.sample_rate = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--period-us") && i + 1 < argc) cfg.period_us   = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(a, "--offline")   && i + 1 < argc) {
            offline_path = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') offline_secs = atof(argv[++i]);
        }
        else { fprintf(stderr, "unknown argument: %s (see the header of main.c)\n", a); return 1; }
    }

    if (list) {
        printf("Output devices (* = default):\n");
        HRESULT hr = backend_enumerate_devices(on_list_device, NULL);
        if (FAILED(hr)) fprintf(stderr, "enumeration failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
        return FAILED(hr);
    }

    if (offline_path) {
        uint32_t rate = cfg.sample_rate ? cfg.sample_rate : 48000;
        engine_init(&g_engine, (float)rate);
        load_default_patch();
        set_crusher(crush);
        set_tremolo(tremolo);
        int rc = run_offline(offline_path, offline_secs, rate);
        engine_shutdown(&g_engine);
        return rc;
    }

    DevicePick pick = { .wanted = device_index };
    if (device_index >= 0) {
        backend_enumerate_devices(on_pick_device, &pick);
        if (!pick.found) { fprintf(stderr, "no device with index %d (try --list)\n", device_index); return 1; }
        cfg.device_id = pick.id;
    }

    // Open first: the device decides the sample rate the engine must run at.
    cfg.callback = engine_render;
    cfg.user     = &g_engine;
    HRESULT hr = backend_open(&g_backend, &cfg);
    if (FAILED(hr)) {
        fprintf(stderr, "backend_open failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
        return 1;
    }
    print_stream_info(&g_backend);

    if (info) { backend_close(&g_backend); return 0; }

    engine_init(&g_engine, (float)g_backend.sample_rate);
    load_default_patch();
    set_crusher(crush);
    set_tremolo(tremolo);

    hr = backend_start(&g_backend);
    if (FAILED(hr)) {
        fprintf(stderr, "backend_start failed: 0x%08lX %s\n", hr, backend_hr_string(hr));
        backend_close(&g_backend);
        engine_shutdown(&g_engine);
        return 1;
    }

    int rc = run_interactive(crush, tremolo);

    backend_stop(&g_backend);       // joins the audio thread — engine is idle after this
    backend_close(&g_backend);
    engine_shutdown(&g_engine);
    return rc;
}
