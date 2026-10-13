#include "io/wav_reader/wav_reader.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define FORMAT_PCM         0x0001
#define FORMAT_FLOAT       0x0003
#define FORMAT_EXTENSIBLE  0xFFFE

typedef struct {
    uint32_t format;
    uint32_t channels;
    uint32_t rate;
    uint32_t bits;
    uint32_t block_align;
} Format;

// Little-endian reads, independent of the machine's byte order and alignment.
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int16_t to_int16(float x) {
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    return (int16_t)lrintf(x * 32767.0f);
}

// One sample at `p`, as a float in -1..+1, for the given format.
static float read_sample(const uint8_t* p, const Format* f) {
    if (f->format == FORMAT_FLOAT) {
        if (f->bits == 32) {
            uint32_t raw = u32(p);
            float    x;
            memcpy(&x, &raw, sizeof x);
            return x;
        }
        uint64_t raw = (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32);     // 64-bit float
        double   x;
        memcpy(&x, &raw, sizeof x);
        return (float)x;
    }

    switch (f->bits) {
    case 8:  return ((int)p[0] - 128) / 128.0f;                             // 8-bit WAV is unsigned
    case 16: return (int16_t)u16(p) / 32768.0f;
    case 24: return ((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8) / 8388608.0f;
    default: return (int32_t)u32(p) / 2147483648.0f;                        // 32
    }
}

static bool check_format(const Format* f, char* error, size_t cap) {
    if (f->channels < 1 || f->channels > 64) {
        audio_clip_set_error(error, cap, "WAV: %u channels is not supported", f->channels);
        return false;
    }
    if (f->rate < 1000 || f->rate > 384000) {
        audio_clip_set_error(error, cap, "WAV: sample rate %u Hz is not supported", f->rate);
        return false;
    }
    bool pcm_ok   = f->format == FORMAT_PCM   && (f->bits == 8 || f->bits == 16 || f->bits == 24 || f->bits == 32);
    bool float_ok = f->format == FORMAT_FLOAT && (f->bits == 32 || f->bits == 64);
    if (!pcm_ok && !float_ok) {
        audio_clip_set_error(error, cap, f->format == FORMAT_PCM || f->format == FORMAT_FLOAT
                                         ? "WAV: %u-bit samples are not supported"
                                         : "WAV: compressed format 0x%04X is not supported (use plain PCM)",
                             f->format == FORMAT_PCM || f->format == FORMAT_FLOAT ? f->bits : f->format);
        return false;
    }
    if (f->block_align < f->channels * (f->bits / 8)) {
        audio_clip_set_error(error, cap, "WAV: inconsistent block size");
        return false;
    }
    return true;
}

bool wav_load_memory(const uint8_t* b, size_t size, AT_Clip* out, char* error, size_t error_cap) {
    memset(out, 0, sizeof *out);

    if (size < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
        audio_clip_set_error(error, error_cap, "not a WAV file (no RIFF/WAVE header)");
        return false;
    }

    Format         fmt      = { 0 };
    bool           have_fmt = false;
    const uint8_t* data     = NULL;
    size_t         data_len = 0;

    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t* id        = b + pos;
        size_t         chunk_len = u32(b + pos + 4);
        const uint8_t* body      = b + pos + 8;
        size_t         room      = size - (pos + 8);          // bytes really left in the file

        if (!memcmp(id, "fmt ", 4) && chunk_len >= 16 && room >= 16) {
            fmt.format      = u16(body);
            fmt.channels    = u16(body + 2);
            fmt.rate        = u32(body + 4);
            fmt.block_align = u16(body + 12);
            fmt.bits        = u16(body + 14);
            if (fmt.format == FORMAT_EXTENSIBLE && chunk_len >= 26 && room >= 26)
                fmt.format = u16(body + 24);                  // first two bytes of the sub-format GUID
            have_fmt = true;
        } else if (!memcmp(id, "data", 4)) {
            data     = body;
            data_len = chunk_len > room ? room : chunk_len;   // cut-short or never-finished file
            if (chunk_len == 0 || chunk_len == 0xFFFFFFFFu) data_len = room;
            break;                                            // everything we need is known by now
        }

        size_t next = pos + 8 + chunk_len + (chunk_len & 1);  // chunks are padded to even sizes
        if (next <= pos) break;
        pos = next;
    }

    if (!have_fmt) { audio_clip_set_error(error, error_cap, "WAV: no fmt chunk"); return false; }
    if (!data)     { audio_clip_set_error(error, error_cap, "WAV: no data chunk"); return false; }
    if (!check_format(&fmt, error, error_cap)) return false;

    uint32_t frames = (uint32_t)(data_len / fmt.block_align);
    if (frames == 0) { audio_clip_set_error(error, error_cap, "WAV: the file contains no audio"); return false; }

    uint32_t out_channels = fmt.channels == 1 ? 1 : 2;       // more than two: keep front left + right
    if (!audio_clip_alloc(out, frames, out_channels, fmt.rate)) {
        audio_clip_set_error(error, error_cap, "WAV: out of memory (%u frames)", frames);
        return false;
    }

    const uint32_t bytes = fmt.bits / 8;
    int16_t*       dst   = out->data;
    for (uint32_t i = 0; i < frames; i++) {
        const uint8_t* frame = data + (size_t)i * fmt.block_align;
        for (uint32_t c = 0; c < out_channels; c++)
            *dst++ = to_int16(read_sample(frame + c * bytes, &fmt));
    }
    return true;
}

bool wav_load(const char* path, AT_Clip* out, char* error, size_t error_cap) {
    size_t   size  = 0;
    uint8_t* bytes = audio_clip_read_file(path, &size, error, error_cap);
    if (!bytes) { memset(out, 0, sizeof *out); return false; }

    bool ok = wav_load_memory(bytes, size, out, error, error_cap);
    free(bytes);
    return ok;
}
