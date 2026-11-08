/* hello-beeper — root application of the child-beeper toolkit.
 *
 * Brings the board up through the cb_bsp board support package, draws a simple
 * screen (solid background + label) once the display stack is up, and prints a
 * heartbeat over the USB-Serial/JTAG console (the Type-C port; console selected
 * by CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG in config/sdkconfig.board.defaults).
 *
 * With item 2 landed, cb_bsp_init(CB_PART_ALL) returns ESP_OK on a good board
 * (LCD + LVGL are the strict parts and are implemented; RGB/I2C/IMU/BUTTON/
 * BATTERY/SD are optional and still log "not implemented" until their items
 * land — the app keeps running regardless).
 *
 * Boot summary: the USB host only re-attaches the serial console ~1 s after
 * reset (measured on this board), so the early boot logs are usually lost.
 * After the FIRST heartbeat, once the console is observable, a compact one-shot
 * summary is printed: reset reason, cb_bsp version, the stored
 * cb_bsp_init(CB_PART_ALL) result and the IDF version.
 *
 * The backlight is not touched here: cb_lcd raises it once, after the first
 * rendered frame (ruling A2).
 */
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"

#include "cb_bsp.h"
#include "cb_lcd.h"
#include "cb_pattern.h"
#include "cb_pins.h"
#include "lvgl.h"

static const char *TAG = "hello_beeper";

#define CANVAS_BYTES (CB_LCD_H_RES * CB_LCD_V_RES * (int)sizeof(uint16_t))

/* Compact name for the reset reason (esp_reset_reason has no to_name helper). */
static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:    return "POWERON";
    case ESP_RST_EXT:        return "EXT";
    case ESP_RST_SW:         return "SW";
    case ESP_RST_PANIC:      return "PANIC";
    case ESP_RST_INT_WDT:    return "INT_WDT";
    case ESP_RST_TASK_WDT:   return "TASK_WDT";
    case ESP_RST_WDT:        return "WDT";
    case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:   return "BROWNOUT";
    case ESP_RST_SDIO:       return "SDIO";
    case ESP_RST_USB:        return "USB";
    case ESP_RST_JTAG:       return "JTAG";
    case ESP_RST_EFUSE:      return "EFUSE";
    case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
    default:                 return "UNKNOWN";
    }
}

/* Simple screen: solid dark background + a label. Caller holds the LVGL lock. */
static void draw_screen(void)
{
    uint16_t *canvas = heap_caps_malloc(CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (canvas == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the %d-byte canvas", CANVAS_BYTES);
        return;
    }
    cb_pattern_fill_solid(canvas, CB_LCD_H_RES, CB_LCD_V_RES,
                          0x10C4);   /* RGB565 for #101820 (dark blue-grey) */

    static lv_image_dsc_t dsc;
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.stride = CB_LCD_H_RES * 2;
    dsc.header.w = CB_LCD_H_RES;
    dsc.header.h = CB_LCD_V_RES;
    dsc.data_size = CANVAS_BYTES;
    dsc.data = (const uint8_t *)canvas;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &dsc);
    lv_obj_set_pos(img, 0, 0);

    lv_obj_t *lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(lbl, "hello beeper\ncb_bsp %s", cb_bsp_version());
    lv_obj_center(lbl);
}

void app_main(void)
{
    ESP_LOGI(TAG, "hello beeper — child-beeper toolkit root app");
    ESP_LOGI(TAG, "cb_bsp %s / ESP-IDF %s", cb_bsp_version(), esp_get_idf_version());

    /* Request every part: implemented parts come up in the frozen order, the
     * rest log "not implemented" and are skipped; optional failures never
     * abort. Strict-part (LCD/LVGL) failures are returned. */
    esp_err_t err = cb_bsp_init(CB_PART_ALL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "cb_bsp_init(CB_PART_ALL): all requested parts are up");
    } else {
        ESP_LOGW(TAG, "cb_bsp_init(CB_PART_ALL) -> %s "
                 "(a strict part failed; see the errors above)",
                 esp_err_to_name(err));
    }

    if (err == ESP_OK && cb_lcd_lvgl_display() != NULL) {
        if (cb_lcd_lvgl_lock(1000)) {
            draw_screen();
            cb_lcd_lvgl_unlock();
        } else {
            ESP_LOGE(TAG, "LVGL lock timeout while drawing the screen");
        }
    }
    /* Backlight: owned by cb_lcd's one-shot first-frame hook (ruling A2). */

    for (int i = 1;; ++i) {
        ESP_LOGI(TAG, "alive #%d", i);
        if (i == 1) {
            /* One-shot, after the first heartbeat: the console is reliably
             * attached by now, so the boot state is observable even though
             * the early lines above were lost to the ~1 s re-attach delay.
             * Heap numbers are read here (after the UI is up) so they reflect
             * the running display stack: internal free (after the two DMA draw
             * buffers + LVGL task), PSRAM free (after the 110 KB canvas +
             * LVGL's 64 KB pool is accounted in .bss), and the internal
             * watermark (boot-time low-water mark). */
            ESP_LOGI(TAG,
                     "boot summary: reset=%s cb_bsp=%s init(CB_PART_ALL)=%s idf=%s "
                     "heap int=%uB/%uB psram=%uB",
                     reset_reason_name(esp_reset_reason()),
                     cb_bsp_version(),
                     esp_err_to_name(err),
                     esp_get_idf_version(),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
