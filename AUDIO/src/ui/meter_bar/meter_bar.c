#include "ui/meter_bar/meter_bar.h"

#include "core/at_math.h"

#define METER_FLOOR_DB 60.0f

void meter_bar_format(char* out, int width, float peak) {
    float db     = at_gain_to_db(peak);
    int   filled = (int)((db + METER_FLOOR_DB) / METER_FLOOR_DB * (float)width);
    if (filled < 0)     filled = 0;
    if (filled > width) filled = width;

    for (int i = 0; i < width; i++)
        out[i] = i < filled ? (i >= width - 2 ? '!' : '#') : '.';
    out[width] = '\0';
}
