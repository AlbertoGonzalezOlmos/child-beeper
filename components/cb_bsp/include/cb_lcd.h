/*
 * cb_lcd.h — ST7789 display + LVGL bring-up, item 2. Owner: mimo26-pro
 * (CONTRACT file-ownership). Frozen public surface: item-2 ruling A1 (exactly
 * these six functions; examples must not include esp_lvgl_port.h). Contract:
 * public functions never abort the process (cb_bsp.h).
 */
#ifndef CB_LCD_H
#define CB_LCD_H

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up SPI2 + the ST7789 panel (pins/frequency from cb_pins.h,
 *        skeleton-verified sequence; RGB element order, gap(34,0),
 *        invert_color(true)). Idempotent; returns the first error instead of
 *        aborting (all ESP_ERROR_CHECKs removed).
 */
esp_err_t cb_lcd_init(void);

/**
 * @brief Initialise esp_lvgl_port and add the panel display (RGB565 buffers,
 *        PSRAM + DMA, byte swap, CB_LCD_H_RES*40 pixels, double buffered).
 *        Requires a successful cb_lcd_init() first — otherwise
 *        ESP_ERR_INVALID_STATE (no implicit panel bring-up). Idempotent.
 *
 * After the port display is added, a one-shot LV_EVENT_REFR_READY callback is
 * registered that switches the backlight on (cb_backlight_set(CB_BACKLIGHT_ON_PERCENT))
 * once the first frame is drawn; examples must not enable the backlight
 * themselves (they may dim it later).
 */
esp_err_t cb_lcd_init_lvgl(void);

/**
 * @brief Panel handle from cb_lcd_init(), or NULL if the panel is not up.
 */
esp_lcd_panel_handle_t cb_lcd_panel(void);

/**
 * @brief The lv_display_t registered by cb_lcd_init_lvgl(), or NULL while the
 *        port is not up. Render through LVGL while holding cb_lcd_lvgl_lock().
 */
lv_display_t *cb_lcd_lvgl_display(void);

/**
 * @brief Take the esp_lvgl_port task lock (wrap of lvgl_port_lock). timeout_ms
 *        0 blocks until the lock is available. Returns false on timeout.
 */
bool cb_lcd_lvgl_lock(uint32_t timeout_ms);

/**
 * @brief Release the esp_lvgl_port task lock (wrap of lvgl_port_unlock).
 */
void cb_lcd_lvgl_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_LCD_H */
