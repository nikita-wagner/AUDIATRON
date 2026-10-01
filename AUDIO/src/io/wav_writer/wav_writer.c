#include "io/wav_writer/wav_writer.h"

#include "io/sample_format/sample_format.h"

#define WAV_MAX_CHANNELS 8

static void write_u32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void write_u16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void write_header(const AT_WavWriter* w) {
    const uint32_t block_align = w->channels * sizeof(int16_t);
    const uint32_t data_bytes  = w->frames_written * block_align;
    FILE*          f           = w->file;

    fwrite("RIFF", 1, 4, f); write_u32(f, 36 + data_bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); write_u32(f, 16);
    write_u16(f, 1);                                    // PCM
    write_u16(f, (uint16_t)w->channels);
    write_u32(f, w->sample_rate);
    write_u32(f, w->sample_rate * block_align);
    write_u16(f, (uint16_t)block_align);
    write_u16(f, 16);                                   // bits per sample
    fwrite("data", 1, 4, f); write_u32(f, data_bytes);
}

bool wav_writer_open(AT_WavWriter* w, const char* path, uint32_t sample_rate, uint32_t channels) {
    memset(w, 0, sizeof *w);
    if (channels == 0 || channels > WAV_MAX_CHANNELS) return false;
    w->file = fopen(path, "wb");
    if (!w->file) return false;

    w->sample_rate = sample_rate;
    w->channels    = channels;
    write_header(w);                                    // sizes are patched in wav_writer_close
    return true;
}

bool wav_writer_write(AT_WavWriter* w, const AT_Buffer* buf) {
    if (!w->file || buf->frames > AT_MAX_BLOCK_FRAMES) return false;

    int16_t pcm[AT_MAX_BLOCK_FRAMES * WAV_MAX_CHANNELS];
    sample_format_write((uint8_t*)pcm, AT_SAMPLE_S16, w->channels, buf);

    size_t samples = (size_t)buf->frames * w->channels;
    if (fwrite(pcm, sizeof(int16_t), samples, w->file) != samples) return false;
    w->frames_written += buf->frames;
    return true;
}

void wav_writer_close(AT_WavWriter* w) {
    if (!w->file) return;
    fseek(w->file, 0, SEEK_SET);
    write_header(w);
    fclose(w->file);
    w->file = NULL;
}
