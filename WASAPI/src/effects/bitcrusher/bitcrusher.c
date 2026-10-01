#include "effects/bitcrusher/bitcrusher.h"

#include "core/at_math.h"

void bitcrusher_init(AT_Bitcrusher* bc, uint32_t downsample, float bits) {
    memset(bc, 0, sizeof *bc);
    bitcrusher_set_param(bc, AT_BITCRUSHER_PARAM_DOWNSAMPLE, (float)downsample);
    bitcrusher_set_param(bc, AT_BITCRUSHER_PARAM_BITS, bits);
}

void bitcrusher_set_param(void* self, int param, float value) {
    AT_Bitcrusher* bc = (AT_Bitcrusher*)self;

    switch ((AT_BitcrusherParam)param) {
    case AT_BITCRUSHER_PARAM_DOWNSAMPLE:
        bc->downsample = value < 1.0f ? 1u : (uint32_t)value;
        break;
    case AT_BITCRUSHER_PARAM_BITS:
        bc->levels = powf(2.0f, at_clampf(value, 1.0f, 16.0f) - 1.0f);
        break;
    }
}

void bitcrusher_process(AT_Buffer* buf, void* user) {
    AT_Bitcrusher* bc = (AT_Bitcrusher*)user;

    for (uint32_t i = 0; i < buf->frames; i++) {
        bool take = (bc->counter++ % bc->downsample) == 0;
        for (uint32_t c = 0; c < buf->channels; c++) {
            if (take) bc->held[c] = roundf(buf->ch[c][i] * bc->levels) / bc->levels;
            buf->ch[c][i] = bc->held[c];
        }
    }
}
