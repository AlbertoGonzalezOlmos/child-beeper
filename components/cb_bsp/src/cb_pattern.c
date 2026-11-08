#include "cb_pattern.h"

static const uint16_t k_bar_colors[8] = {
    0xFFFF, /* white */
    0xFFE0, /* yellow */
    0x07FF, /* cyan */
    0x07E0, /* green */
    0xF81F, /* magenta */
    0xF800, /* red */
    0x001F, /* blue */
    0x0000  /* black */
};

uint16_t cb_pattern_bar_color(int index)
{
    if (index < 0 || index >= 8) {
        return 0x0000;
    }
    return k_bar_colors[index];
}

void cb_pattern_fill_solid(uint16_t *dst, int width, int height, uint16_t rgb565)
{
    if (dst == 0 || width <= 16 || height <= 16) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            dst[y * width + x] = rgb565;
        }
    }
}

void cb_pattern_fill_diagnostic(uint16_t *dst, int width, int height)
{
    if (dst == 0 || width <= 16 || height <= 16) {
        return;
    }

    const int ramp_height = height / 16;
    const int ramp_top = height - ramp_height;

    for (int y = 0; y < ramp_top; ++y) {
        for (int x = 0; x < width; ++x) {
            const int bar = (int)((int64_t)x * 8 / width);
            dst[y * width + x] = cb_pattern_bar_color(bar);
        }
    }

    for (int y = ramp_top; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t v = (uint8_t)((int64_t)x * 255 / (width - 1));
            const uint16_t grey = (uint16_t)(((uint16_t)(v >> 3) << 11) |
                                             ((uint16_t)(v >> 2) << 5) |
                                             (uint16_t)(v >> 3));
            dst[y * width + x] = grey;
        }
    }

    /* Four 2x2 corner markers, drawn last to expose clipping/mirroring. */
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            dst[dy * width + dx] = 0xF800;
            dst[dy * width + (width - 2 + dx)] = 0xFFE0;
            dst[(height - 2 + dy) * width + dx] = 0x07FF;
            dst[(height - 2 + dy) * width + (width - 2 + dx)] = 0xF81F;
        }
    }
}
