/*
 * cb_backlight.h — LCD backlight (GPIO46, ST7789 LEDK PWM), item 2. Owner:
 * mimo26-pro (CONTRACT file-ownership). Header form matches cb_math.h /
 * cb_button_fsm.h (include guard, extern "C", doc comments on every public
 * symbol). Contract: public functions never abort the process (cb_bsp.h).
 */
#ifndef CB_BACKLIGHT_H
#define CB_BACKLIGHT_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure GPIO46 as LEDC PWM output (CB_LCD_BL_PWM_HZ, 10-bit) and
 *        start at duty 0 so there is no white flash at boot (GPIO46 is also a
 *        strapping pin; hold-low at boot is fine).
 *
 * Idempotent: a second call is a logged no-op returning ESP_OK. On failure the
 * pin is deconfigured and the error is returned (retry is allowed).
 */
esp_err_t cb_backlight_init(void);

/**
 * @brief Set backlight brightness. @p percent is clamped to 0..100 and
 *        converted with cb_math_duty_from_percent(percent,
 *        CB_MATH_BACKLIGHT_MAX). Before cb_backlight_init() this is a logged
 *        no-op (contract: never aborts).
 */
void cb_backlight_set(int percent);

/**
 * @brief Last percent requested via cb_backlight_set(), clamped 0..100.
 *        0 before the first set; not the measured duty.
 */
int cb_backlight_get(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_BACKLIGHT_H */
