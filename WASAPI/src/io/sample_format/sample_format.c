#include "io/sample_format/sample_format.h"

#include "core/at_math.h"

const char* sample_format_name(AT_SampleType t) {
    switch (t) {
    case AT_SAMPLE_F32: return "float32";
    case AT_SAMPLE_S16: return "int16";
    case AT_SAMPLE_S24: return "int24 (packed)";
    case AT_SAMPLE_S32: return "int32";
    default:            return "?";
    }
}

uint32_t sample_format_bytes(AT_SampleType t) {
    switch (t) {
    case AT_SAMPLE_S16: return 2;
    case AT_SAMPLE_S24: return 3;
    default:            return 4;
    }
}

static inline float mapped_sample(const AT_Buffer* src, uint32_t dst_channels, uint32_t c, uint32_t i) {
    float x;
    if (dst_channels == 1 && src->channels > 1) x = 0.5f * (src->ch[0][i] + src->ch[1][i]);   // mono downmix
    else if (c < src->channels)                 x = src->ch[c][i];
    else                                        x = 0.0f;                                      // extra channels
    return at_clampf(x, -1.0f, 1.0f);
}

void sample_format_write(uint8_t* dst, AT_SampleType type, uint32_t dst_channels,
                         const AT_Buffer* src) {
    const uint32_t n = src->frames;

    switch (type) {
    case AT_SAMPLE_F32: {
        float* out = (float*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dst_channels; c++)
                *out++ = mapped_sample(src, dst_channels, c, i);
        break;
    }
    case AT_SAMPLE_S16: {
        int16_t* out = (int16_t*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dst_channels; c++)
                *out++ = (int16_t)lrintf(mapped_sample(src, dst_channels, c, i) * 32767.0f);
        break;
    }
    case AT_SAMPLE_S24: {
        uint8_t* out = dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dst_channels; c++) {
                int32_t v = (int32_t)lrintf(mapped_sample(src, dst_channels, c, i) * 8388607.0f);
                *out++ = (uint8_t)(v);
                *out++ = (uint8_t)(v >> 8);
                *out++ = (uint8_t)(v >> 16);
            }
        break;
    }
    case AT_SAMPLE_S32: {
        // Full-scale 32-bit; a 24-in-32 device simply ignores the low byte.
        // double, because 1.0f * 2147483647.0f rounds up to 2^31 and overflows.
        int32_t* out = (int32_t*)dst;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t c = 0; c < dst_channels; c++)
                *out++ = (int32_t)((double)mapped_sample(src, dst_channels, c, i) * 2147483647.0);
        break;
    }
    }
}
