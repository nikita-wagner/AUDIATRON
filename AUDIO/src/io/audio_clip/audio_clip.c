#include "io/audio_clip/audio_clip.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_BYTES (512u * 1024u * 1024u)       // sanity limit; real files are far smaller

void audio_clip_set_error(char* error, size_t cap, const char* fmt, ...) {
    if (!error || cap == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, cap, fmt, args);
    va_end(args);
}

bool audio_clip_alloc(AT_Clip* c, uint32_t frames, uint32_t channels, uint32_t sample_rate) {
    memset(c, 0, sizeof *c);
    if (channels < 1 || channels > 2 || sample_rate == 0) return false;

    size_t samples = (size_t)frames * channels;
    if (samples / channels != frames) return false;                 // overflow
    c->data = (int16_t*)calloc(samples ? samples : 1, sizeof(int16_t));
    if (!c->data) return false;

    c->frames      = frames;
    c->channels    = channels;
    c->sample_rate = sample_rate;
    return true;
}

void audio_clip_adopt(AT_Clip* c, int16_t* data, uint32_t frames, uint32_t channels, uint32_t sample_rate) {
    c->data        = data;
    c->frames      = frames;
    c->channels    = channels;
    c->sample_rate = sample_rate;
}

void audio_clip_free(AT_Clip* c) {
    free(c->data);
    memset(c, 0, sizeof *c);
}

uint8_t* audio_clip_read_file(const char* path, size_t* size, char* error, size_t error_cap) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        audio_clip_set_error(error, error_cap, "cannot open %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length <= 0 || (unsigned long)length > MAX_FILE_BYTES) {
        fclose(f);
        audio_clip_set_error(error, error_cap, "%s is empty or too large", path);
        return NULL;
    }

    uint8_t* bytes = (uint8_t*)malloc((size_t)length);
    if (!bytes || fread(bytes, 1, (size_t)length, f) != (size_t)length) {
        free(bytes);
        fclose(f);
        audio_clip_set_error(error, error_cap, "cannot read %s", path);
        return NULL;
    }
    fclose(f);

    *size = (size_t)length;
    return bytes;
}
