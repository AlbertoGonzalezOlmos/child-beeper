/*
 * cb_rgb.h — single addressable RGB LED (WS2812-compatible, GRB) on
 * CB_PIN_RGB, item 3. Owner: deepseek-pro (CONTRACT file-ownership). Header
 * form matches cb_math.h / cb_backlight.h (include guard, extern "C", doc
 * comments on every public symbol). Contract: public functions never abort
 * the process (optional part).
 */
#ifndef CB_RGB_H
#define CB_RGB_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the led_strip 3.1.1 RMT device on CB_PIN_RGB (one pixel,
 *        GRB component order, 10 MHz) and clear the pixel through the same
 *        set+refresh path (boots dark — ruling R10b). Idempotent: a second
 *        call logs "already initialized" and returns ESP_OK. On failure the
 *        device/mutex are torn down and the error is returned (retry
 *        allowed); never aborts.
 */
esp_err_t cb_rgb_init(void);

/**
 * @brief Dim @p r/@p g/@p b with cb_rgb_math_dim (peak-scale to
 *        CB_RGB_DIM_CAP_PERCENT, ruling R1b) and write + refresh the pixel
 *        under the led_strip mutex (led_strip is not re-entrant; the wait
 *        is bounded to 100 ms, ruling R4b). Returns the first driver error;
 *        ESP_ERR_INVALID_STATE before cb_rgb_init().
 */
esp_err_t cb_rgb_set(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief cb_rgb_set(0, 0, 0) through the same mutex-guarded path.
 */
esp_err_t cb_rgb_off(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_RGB_H */
