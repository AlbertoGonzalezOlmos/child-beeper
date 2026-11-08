/* 01_lcd_hello — display-stack diagnostic example (item 2).
 *
 * Brings up backlight + LCD + LVGL via the BSP, then shows the diagnostic
 * pattern (kimi's cb_pattern): 8 vertical colour bars (white, yellow, cyan,
 * green, magenta, red, blue, black), a full-width grey ramp at the bottom and
 * 2x2 markers in all four corners — full-screen, so on hardware it immediately
 * exposes x-gap (34), RGB/BGR order, inversion and mirroring errors.
 *
 * Serial contract (frozen, ruling A5):
 *   lcd_hello: display: 172x320 ST7789, spi2 @40MHz, gap(34,0), lvgl 9.5
 *   lcd_hello: frame N        (every 2 s, N = 1, 2, ...)
 *
 * Backlight is NOT touched here: cb_lcd raises it to CB_BACKLIGHT_ON_PERCENT
 * once, via its one-shot first-frame hook (ruling A2).
 *
 * Every lv_* call happens inside cb_lcd_lvgl_lock()/unlock(); lock timeouts in
 * the heartbeat loop are finite so a wedged LVGL task cannot silence the
 * serial heartbeat. Build-verified only until the orchestrator's flash
 * (see docs/verification.md).
 */
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "cb_bsp.h"
#include "cb_lcd.h"
#include "cb_pattern.h"
#include "cb_pins.h"
#include "lvgl.h"

static const char *TAG = "lcd_hello";

#define CANVAS_BYTES (CB_LCD_H_RES * CB_LCD_V_RES * (int)sizeof(uint16_t))

static lv_image_dsc_t s_pattern_dsc;   /* filled once, points at the PSRAM canvas */
static lv_obj_t *s_label;              /* updated every heartbeat under the lock */

/* Build the UI. Caller holds the LVGL lock. */
static bool build_ui(void)
{
    uint16_t *canvas = heap_caps_malloc(CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvas == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the %d-byte canvas", CANVAS_BYTES);
        return false;
    }
    cb_pattern_fill_diagnostic(canvas, CB_LCD_H_RES, CB_LCD_V_RES);

    s_pattern_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_pattern_dsc.header.cf = LV_COLOR_FORMAT_RGB565;   /* native-endian: matches cb_pattern */
    s_pattern_dsc.header.stride = CB_LCD_H_RES * 2;
    s_pattern_dsc.header.w = CB_LCD_H_RES;
    s_pattern_dsc.header.h = CB_LCD_V_RES;
    s_pattern_dsc.data_size = CANVAS_BYTES;
    s_pattern_dsc.data = (const uint8_t *)canvas;

    lv_obj_t *scr = lv_screen_active();

    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &s_pattern_dsc);
    lv_obj_set_pos(img, 0, 0);   /* full-screen: the pattern's corner markers sit at the panel edges */

    /* Opaque label box near the bottom; inset so all four corner markers stay visible. */
    s_label = lv_label_create(scr);
    lv_obj_set_style_bg_color(s_label, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(s_label, "%dx%d\nframe 0", CB_LCD_H_RES, CB_LCD_V_RES);
    lv_obj_align(s_label, LV_ALIGN_BOTTOM_MID, 0, -8);

    return true;
}

void app_main(void)
{
    esp_err_t err = cb_bsp_init(CB_PART_BACKLIGHT | CB_PART_LCD | CB_PART_LVGL);

    ESP_LOGI(TAG, "display: %dx%d ST7789, spi2 @%dMHz, gap(%d,%d), lvgl %d.%d",
             CB_LCD_H_RES, CB_LCD_V_RES, CB_LCD_PIXEL_CLK_HZ / 1000000,
             CB_LCD_X_GAP, CB_LCD_Y_GAP,
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cb_bsp_init(BACKLIGHT|LCD|LVGL) -> %s "
                 "(display stack down; serial heartbeat only)",
                 esp_err_to_name(err));
    }

    bool ui_up = false;
    if (cb_lcd_lvgl_display() != NULL) {
        if (cb_lcd_lvgl_lock(1000)) {
            ui_up = build_ui();
            cb_lcd_lvgl_unlock();
        } else {
            ESP_LOGE(TAG, "LVGL lock timeout while building the UI");
        }
    }
    /* Backlight: owned by cb_lcd's one-shot first-frame hook (ruling A2). */

    for (unsigned frame = 1;; ++frame) {
        ESP_LOGI(TAG, "frame %u", frame);
        if (ui_up && cb_lcd_lvgl_lock(100)) {
            lv_label_set_text_fmt(s_label, "%dx%d\nframe %u",
                                  CB_LCD_H_RES, CB_LCD_V_RES, frame);
            cb_lcd_lvgl_unlock();   /* frame counter on screen proves the render loop is live */
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
