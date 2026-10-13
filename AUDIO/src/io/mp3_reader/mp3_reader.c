// minimp3 is written for C99 compilers with warnings turned down; keep our
// warning-free build by silencing diagnostics for its code only.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO        // no file mapping: we read the file ourselves
#define MINIMP3_NO_SIMD         // portable C only (no SSE: also runs on old CPUs)
#define MINIMP3_ONLY_MP3        // Layer III only; drops the Layer I/II decoders
#include "third_party/minimp3/minimp3_ex.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "io/mp3_reader/mp3_reader.h"

#include <stdlib.h>
#include <string.h>

bool mp3_load_memory(const uint8_t* bytes, size_t size, AT_Clip* out, char* error, size_t error_cap) {
    memset(out, 0, sizeof *out);

    mp3dec_t           decoder;
    mp3dec_file_info_t info;
    memset(&info, 0, sizeof info);

    mp3dec_init(&decoder);
    int status = mp3dec_load_buf(&decoder, bytes, size, &info, NULL, NULL);

    if (status != 0 || !info.buffer || info.samples == 0 || info.channels < 1 || info.hz == 0) {
        free(info.buffer);
        audio_clip_set_error(error, error_cap, "not a decodable MP3 file");
        return false;
    }
    if (info.channels > 2 || sizeof(mp3d_sample_t) != sizeof(int16_t)) {
        free(info.buffer);
        audio_clip_set_error(error, error_cap, "MP3: %d channels is not supported", info.channels);
        return false;
    }

    audio_clip_adopt(out, (int16_t*)info.buffer, (uint32_t)(info.samples / (size_t)info.channels),
                     (uint32_t)info.channels, (uint32_t)info.hz);
    return true;
}

bool mp3_load(const char* path, AT_Clip* out, char* error, size_t error_cap) {
    size_t   size  = 0;
    uint8_t* bytes = audio_clip_read_file(path, &size, error, error_cap);
    if (!bytes) { memset(out, 0, sizeof *out); return false; }

    bool ok = mp3_load_memory(bytes, size, out, error, error_cap);
    free(bytes);
    return ok;
}
