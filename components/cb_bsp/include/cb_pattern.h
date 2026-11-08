#ifndef CB_PATTERN_H
#define CB_PATTERN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Packed native-endian RGB565 frames, row-major, stride = width * 2 bytes.
 * Width and height must be greater than 16; invalid input is a no-op. */
void cb_pattern_fill_diagnostic(uint16_t *dst, int width, int height);
void cb_pattern_fill_solid(uint16_t *dst, int width, int height, uint16_t rgb565);

/* Bar index 0..7; out-of-range indices return black (0x0000). */
uint16_t cb_pattern_bar_color(int index);

#ifdef __cplusplus
}
#endif

#endif /* CB_PATTERN_H */
