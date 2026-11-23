/*
 * cb_rgb_math.h — RGB dim-cap math, item 3. Owner: deepseek-pro (CONTRACT
 * file-ownership). Pure C, no ESP-IDF/FreeRTOS includes: host-tested and
 * shared with the Arduino tree (item 7).
 *
 * Frozen semantics (item-3 ruling R1b): peak-scale to the cap — the three
 * channels are scaled only when their peak exceeds cap = 255 *
 * CB_RGB_DIM_CAP_PERCENT / 100 (= 51), so the hue is preserved and the
 * operation is idempotent. Values at or below the cap pass through
 * unchanged. NULL pointers are a no-op.
 */
#ifndef CB_RGB_MATH_H
#define CB_RGB_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CB_RGB_DIM_CAP_PERCENT 20  /* max intensity as percent of full scale */

/**
 * @brief Dim @p r/@p g/@p b to CB_RGB_DIM_CAP_PERCENT of full scale,
 *        hue-preserving and idempotent (R1b): with peak = max(r,g,b), the
 *        values are unchanged while peak <= cap (255 *
 *        CB_RGB_DIM_CAP_PERCENT / 100); above it every channel is scaled by
 *        cap/peak with floor rounding (32-bit math). NULL pointers are a
 *        no-op.
 */
void cb_rgb_math_dim(uint8_t *r, uint8_t *g, uint8_t *b);

#ifdef __cplusplus
}
#endif

#endif /* CB_RGB_MATH_H */
