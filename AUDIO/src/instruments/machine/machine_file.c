// ═══════════════════════════════════════════════════════════════════════════
//  instruments/machine/machine_file.c — a machine's setup as text (.atengine)
//
//  Everything that makes a machine sound the way it does, one line each, so an
//  engine you set up can be saved, kept in a library, shared and loaded again:
//
//      # AUDIATRON engine
//      audiatron engine 1
//      sound builtin jet                     or: sound file C:\sounds\diesel.wav
//      volume 1                              follow_ms 900   crossfade_ms 40
//      rise 0.6   fall 0.9   idle_rate 0.4   auto_idle off
//      curve gain 0.25 1 ease_in 1.6 0.25 0.5 0.75
//                (name idle full shape amount point1 point2 point3)
//      fx eq on
//      gearbox mode automatic                gearbox gears 6   gearbox idle_rpm 900 ...
//      gearbox shift_point 0.5 1 ease_in 2 0.25 0.5 0.75
//      gear 1 130 6200 0 70 140 220 0.85 0 1 ease_out 2 0.25 0.5 0.75
//                (n ratio up_rpm down_rpm clutch_out_ms shift_ms clutch_in_ms lift  momentum curve)
//
//  `parts` (AT_MACHINE_SAVE_*) choose what is written or read, e.g. only the
//  gearbox, to put one gearbox under several engines. Lines a reader does not
//  know are skipped, so files stay readable when new settings are added. A
//  sound file that is not where the line says is looked for next to the
//  preset, so a folder with a preset and its WAV can be moved as a whole.
// ═══════════════════════════════════════════════════════════════════════════

#include "instruments/machine/machine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/at_math.h"

static const char* const k_shapes[AT_MACHINE_SHAPES] = { "linear", "ease_in", "ease_out", "s_curve", "custom" };

static const char* on_off(bool on) { return on ? "on" : "off"; }

static void set_error(char* error, size_t cap, const char* fmt, ...) {
    if (!error || cap == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, cap, fmt, args);
    va_end(args);
}

// ─── Writing ─────────────────────────────────────────────────────────────────

// One line being put together, handed to `emit` when complete.
typedef struct {
    char   text[512];
    size_t len;
    void (*emit)(void* user, const char* line);
    void*  user;
} Line;

static void add(Line* l, const char* fmt, ...) {
    if (l->len >= sizeof l->text - 1) return;
    va_list args;
    va_start(args, fmt);
    const int n = vsnprintf(l->text + l->len, sizeof l->text - l->len, fmt, args);
    va_end(args);
    if (n > 0) l->len = l->len + (size_t)n < sizeof l->text ? l->len + (size_t)n : sizeof l->text - 1;
}

static void done(Line* l) {
    l->emit(l->user, l->text);
    l->len     = 0;
    l->text[0] = '\0';
}

// " 0.35": the shortest text that reads back as exactly this float, so a
// file stays easy to edit by hand and loads bit for bit what was saved.
static void put(Line* l, float v) {
    char text[32];
    for (int digits = 6; digits <= 9; digits++) {
        snprintf(text, sizeof text, "%.*g", digits, v);
        if ((float)strtod(text, NULL) == v) break;
    }
    add(l, " %s", text);
}

static void put_line(Line* l, const char* key, float v) {
    add(l, "%s", key);
    put(l, v);
    done(l);
}

static void put_curve(Line* l, const AT_MachineCurve* c) {
    put(l, c->idle);
    put(l, c->full);
    add(l, " %s", k_shapes[c->shape]);
    put(l, c->amount);
    for (int i = 0; i < 3; i++) put(l, c->points[i]);
}

void machine_settings_snapshot(const AT_Machine* m, unsigned parts, void (*emit)(void* user, const char* line), void* user) {
    Line l = { "", 0, emit, user };
    if (parts & AT_MACHINE_SAVE_SOUND) {
        if (m->path[0]) add(&l, "sound file %s", m->path);
        else            add(&l, "sound builtin %s", AT_MACHINE_BUILTIN_NAMES[m->builtin]);
        done(&l);
    }
    if (parts & AT_MACHINE_SAVE_MOTION) {
        put_line(&l, "volume", m->volume);
        put_line(&l, "follow_ms", m->smooth_ms);
        put_line(&l, "crossfade_ms", m->xfade_ms);
        put_line(&l, "rise", m->rise_per_s);
        put_line(&l, "fall", m->fall_per_s);
        put_line(&l, "idle_rate", m->idle_per_s);
        add(&l, "auto_idle %s", on_off(m->auto_idle));
        done(&l);
    }
    if (parts & AT_MACHINE_SAVE_CURVES)
        for (int i = 0; i < AT_MACHINE_CURVES; i++) {
            add(&l, "curve %s", AT_MACHINE_CURVE_NAMES[i]);
            put_curve(&l, &m->curves[i]);
            done(&l);
        }
    if (parts & AT_MACHINE_SAVE_FX)
        for (int i = 0; i < AT_MACHINE_FX_COUNT; i++) {
            add(&l, "fx %s %s", AT_MACHINE_FX_NAMES[i], on_off(m->fx[i]));
            done(&l);
        }
    if (parts & AT_MACHINE_SAVE_GEARBOX) {
        const AT_MachineGearbox* g = &m->gearbox;
        add(&l, "gearbox gears %d", g->gears);
        done(&l);
        put_line(&l, "gearbox idle_rpm", g->idle_rpm);
        put_line(&l, "gearbox max_rpm", g->max_rpm);
        put_line(&l, "gearbox rev_up_s", g->rev_up_s);
        put_line(&l, "gearbox rev_down_s", g->rev_down_s);
        put_line(&l, "gearbox power", g->power);
        put_line(&l, "gearbox drag", g->drag);
        put_line(&l, "gearbox engine_brake", g->engine_brake);
        put_line(&l, "gearbox brake_power", g->brake_power);
        add(&l, "gearbox shift_point");
        put_curve(&l, &g->shift_point);
        done(&l);
        for (int i = 0; i <= AT_MACHINE_GEARS; i++) {         // (the last: reverse, "gear R")
            const AT_MachineGear* gear = i < AT_MACHINE_GEARS ? &g->gear[i] : &g->reverse;
            const float fields[7] = { gear->ratio, gear->up_rpm, gear->down_rpm, gear->clutch_out_ms, gear->shift_ms, gear->clutch_in_ms, gear->lift };
            if (i < AT_MACHINE_GEARS) add(&l, "gear %d", i + 1);
            else                      add(&l, "gear R");
            for (int k = 0; k < 7; k++) put(&l, fields[k]);
            put_curve(&l, &gear->momentum);
            done(&l);
        }
        add(&l, "gearbox mode %s", AT_MACHINE_GEARBOX_MODE_NAMES[g->mode]);     // last: it starts the drive
        done(&l);
    }
}

static void line_to_file(void* user, const char* line) {
    fprintf((FILE*)user, "%s\n", line);
}

void machine_write_settings(const AT_Machine* m, FILE* f, unsigned parts) {
    machine_settings_snapshot(m, parts, line_to_file, f);
}

bool machine_save_preset(const AT_Machine* m, const char* path, unsigned parts, char* error, size_t error_cap) {
    FILE* f = fopen(path, "w");
    if (!f) { set_error(error, error_cap, "cannot write %s", path); return false; }
    fprintf(f, "# AUDIATRON engine: a machine's sound and setup (instruments/machine/machine_file.c)\n");
    fprintf(f, "audiatron engine 1\n");
    machine_write_settings(m, f, parts);
    const bool ok = !ferror(f);
    fclose(f);
    if (!ok) set_error(error, error_cap, "write error on %s", path);
    return ok;
}

// ─── Reading ─────────────────────────────────────────────────────────────────

static bool shape_of(const char* word, AT_MachineShape* out) {
    for (int i = 0; i < AT_MACHINE_SHAPES; i++)
        if (!strcmp(word, k_shapes[i])) { *out = (AT_MachineShape)i; return true; }
    return false;
}

// "idle full shape amount p1 p2 p3"
static bool read_curve_fields(const char* text, AT_MachineCurve* c) {
    char shape[32];
    AT_MachineCurve v = *c;
    if (sscanf(text, " %f %f %31s %f %f %f %f", &v.idle, &v.full, shape, &v.amount, &v.points[0], &v.points[1], &v.points[2]) != 7) return false;
    if (!shape_of(shape, &v.shape)) return false;
    *c = v;
    return true;
}

static bool read_on(const char* word) { return !strcmp(word, "on") || !strcmp(word, "1"); }

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

bool machine_read_setting(AT_Machine* m, const char* line, unsigned parts, const char* base_dir, char* error, size_t error_cap) {
    char key[32] = "", word[64] = "";
    int  used = 0;
    while (*line == ' ' || *line == '\t') line++;
    if (!*line || *line == '#' || sscanf(line, "%31s%n", key, &used) != 1) return true;
    const char* rest = line + used;
    while (*rest == ' ' || *rest == '\t') rest++;
    float v = 0.0f;
    const bool number = sscanf(rest, "%f", &v) == 1;

    if (!strcmp(key, "sound")) {
        if (!(parts & AT_MACHINE_SAVE_SOUND)) return true;
        int n = 0;
        if (sscanf(rest, "%63s%n", word, &n) != 1) return true;
        const char* what = rest + n;
        while (*what == ' ') what++;
        if (!strcmp(word, "builtin")) {
            AT_MachineBuiltin kind;
            if (!machine_find_builtin(what, &kind)) { set_error(error, error_cap, "no built-in sound \"%s\"", what); return false; }
            if (!machine_load_builtin(m, kind)) { set_error(error, error_cap, "the last sound is still being switched in"); return false; }
            return true;
        }
        if (!strcmp(word, "file")) {
            char path[512];
            snprintf(path, sizeof path, "%s", what);
            size_t len = strlen(path);
            while (len && (path[len - 1] == '\r' || path[len - 1] == '\n' || path[len - 1] == ' ')) path[--len] = '\0';
            if (!file_exists(path) && base_dir) {          // moved together with the preset?
                const char* name = path;
                for (const char* c = path; *c; c++) if (*c == '\\' || *c == '/') name = c + 1;
                char beside[512];
                snprintf(beside, sizeof beside, "%s\\%s", base_dir, name);
                if (file_exists(beside)) snprintf(path, sizeof path, "%s", beside);
            }
            return machine_load(m, path, error, error_cap);
        }
        return true;
    }

    if (parts & AT_MACHINE_SAVE_MOTION) {
        if (!strcmp(key, "volume") && number)       { machine_set_volume(m, v); return true; }
        if (!strcmp(key, "follow_ms") && number)    { machine_set_smooth_ms(m, v); return true; }
        if (!strcmp(key, "crossfade_ms") && number) { machine_set_xfade_ms(m, v); return true; }
        if (!strcmp(key, "rise") && number)         { m->rise_per_s = at_clampf(v, 0.01f, 20.0f); return true; }
        if (!strcmp(key, "fall") && number)         { m->fall_per_s = at_clampf(v, 0.01f, 20.0f); return true; }
        if (!strcmp(key, "idle_rate") && number)    { m->idle_per_s = at_clampf(v, 0.01f, 20.0f); return true; }
        if (!strcmp(key, "auto_idle"))              { m->auto_idle = read_on(rest); return true; }
    }
    if ((parts & AT_MACHINE_SAVE_CURVES) && !strcmp(key, "curve")) {
        int n = 0;
        if (sscanf(rest, "%63s%n", word, &n) != 1) return true;
        for (int i = 0; i < AT_MACHINE_CURVES; i++)
            if (!strcmp(word, AT_MACHINE_CURVE_NAMES[i])) {
                AT_MachineCurve c = m->curves[i];
                if (read_curve_fields(rest + n, &c)) machine_set_curve(m, (AT_MachineCurveId)i, &c);
            }
        return true;
    }
    if ((parts & AT_MACHINE_SAVE_FX) && !strcmp(key, "fx")) {
        char state[16] = "";
        if (sscanf(rest, "%63s %15s", word, state) != 2) return true;
        for (int i = 0; i < AT_MACHINE_FX_COUNT; i++)
            if (!strcmp(word, AT_MACHINE_FX_NAMES[i]) && !machine_set_fx(m, (AT_MachineFx)i, read_on(state))) {
                set_error(error, error_cap, "no memory for the machine's echo");
                return false;
            }
        return true;
    }
    if (parts & AT_MACHINE_SAVE_GEARBOX) {
        AT_MachineGearbox* g = &m->gearbox;
        if (!strcmp(key, "gearbox")) {
            int n = 0;
            if (sscanf(rest, "%63s%n", word, &n) != 1) return true;
            const char* value = rest + n;
            float x = 0.0f;
            const bool num = sscanf(value, "%f", &x) == 1;
            if (!strcmp(word, "mode")) {
                char mode[16] = "";
                sscanf(value, "%15s", mode);
                for (int i = 0; i < 3; i++)
                    if (!strcmp(mode, AT_MACHINE_GEARBOX_MODE_NAMES[i])) machine_set_gearbox_mode(m, (AT_MachineGearboxMode)i);
            }
            else if (!strcmp(word, "gears") && num)        g->gears = (int)at_clampf(x, 1.0f, (float)AT_MACHINE_GEARS);
            else if (!strcmp(word, "idle_rpm") && num)     g->idle_rpm = at_clampf(x, 100.0f, 20000.0f);
            else if (!strcmp(word, "max_rpm") && num)      g->max_rpm = at_clampf(x, 200.0f, 30000.0f);
            else if (!strcmp(word, "rev_up_s") && num)     g->rev_up_s = at_clampf(x, 0.01f, 10.0f);
            else if (!strcmp(word, "rev_down_s") && num)   g->rev_down_s = at_clampf(x, 0.01f, 10.0f);
            else if (!strcmp(word, "power") && num)        g->power = at_clampf(x, 0.0f, 500.0f);
            else if (!strcmp(word, "drag") && num)         g->drag = at_clampf(x, 0.0f, 100.0f);
            else if (!strcmp(word, "engine_brake") && num) g->engine_brake = at_clampf(x, 0.0f, 100.0f);
            else if (!strcmp(word, "brake_power") && num)  g->brake_power = at_clampf(x, 0.0f, 200.0f);
            else if (!strcmp(word, "shift_point"))         read_curve_fields(value, &g->shift_point);
            return true;
        }
        if (!strcmp(key, "gear")) {
            int gear = 0, n = 0;
            AT_MachineGear set;
            AT_MachineGear* into = NULL;
            if (rest[0] == 'R' && (rest[1] == ' ' || rest[1] == '\t'))      // reverse
                into = &g->reverse, n = 1;
            else if (sscanf(rest, "%d%n", &gear, &n) == 1 && gear >= 1 && gear <= AT_MACHINE_GEARS)
                into = &g->gear[gear - 1];
            if (!into) return true;
            set = *into;
            int used2 = 0;
            if (sscanf(rest + n, " %f %f %f %f %f %f %f%n", &set.ratio, &set.up_rpm, &set.down_rpm, &set.clutch_out_ms, &set.shift_ms,
                       &set.clutch_in_ms, &set.lift, &used2) == 7 && read_curve_fields(rest + n + used2, &set.momentum))
                *into = set;
            return true;
        }
    }
    return true;                                            // something else: not this reader's
}

bool machine_load_preset(AT_Machine* m, const char* path, unsigned parts, char* error, size_t error_cap) {
    FILE* f = fopen(path, "r");
    if (!f) { set_error(error, error_cap, "cannot open %s", path); return false; }

    char base[512];
    snprintf(base, sizeof base, "%s", path);
    char* slash = strrchr(base, '\\');
    if (!slash) slash = strrchr(base, '/');
    if (slash) *slash = '\0';
    else       snprintf(base, sizeof base, ".");

    char line[1024];
    bool ok = true, header = false;
    while (ok && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "audiatron engine", 16)) { header = true; continue; }
        ok = machine_read_setting(m, line, parts, base, error, error_cap);
    }
    fclose(f);
    if (ok && !header) { set_error(error, error_cap, "%s is not an AUDIATRON engine", path); return false; }
    return ok;
}
