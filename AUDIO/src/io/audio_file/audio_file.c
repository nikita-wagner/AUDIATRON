#include "io/audio_file/audio_file.h"

#include <stdlib.h>
#include <string.h>

#include "io/mp3_reader/mp3_reader.h"
#include "io/wav_reader/wav_reader.h"

bool audio_file_load_memory(const uint8_t* bytes, size_t size, AT_Clip* out, char* error, size_t error_cap) {
    // WAV announces itself; anything else is handed to the MP3 decoder, which
    // finds its own frames (and skips ID3 tags).
    if (size >= 12 && !memcmp(bytes, "RIFF", 4) && !memcmp(bytes + 8, "WAVE", 4))
        return wav_load_memory(bytes, size, out, error, error_cap);

    if (mp3_load_memory(bytes, size, out, NULL, 0)) return true;

    audio_clip_set_error(error, error_cap, "unknown audio format (supported: WAV, MP3)");
    return false;
}

bool audio_file_load(const char* path, AT_Clip* out, char* error, size_t error_cap) {
    size_t   size  = 0;
    uint8_t* bytes = audio_clip_read_file(path, &size, error, error_cap);
    if (!bytes) { memset(out, 0, sizeof *out); return false; }

    bool ok = audio_file_load_memory(bytes, size, out, error, error_cap);
    free(bytes);
    return ok;
}
