/*
 * cb_rgb_math.c — RGB dim-cap math, item 3. Owner: deepseek-pro (CONTRACT
 * file-ownership). Pure C, host-tested; semantics frozen by item-3 ruling
 * R1b (peak-scale to the cap, floor, idempotent, NULL-safe).
 */
#include "cb_rgb_math.h"

#include <stddef.h>

void cb_rgb_math_dim(uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t peak;
    const uint32_t cap = 255u * CB_RGB_DIM_CAP_PERCENT / 100u;

    if (r == NULL || g == NULL || b == NULL) {
        return;
    }

    peak = *r;
    if (*g > peak) {
        peak = *g;
    }
    if (*b > peak) {
        peak = *b;
    }

    if (peak <= cap) {
        return;  /* at or below the cap: unchanged — dim is idempotent */
    }
    *r = (uint8_t)(((uint32_t)*r * cap) / peak);
    *g = (uint8_t)(((uint32_t)*g * cap) / peak);
    *b = (uint8_t)(((uint32_t)*b * cap) / peak);
}
