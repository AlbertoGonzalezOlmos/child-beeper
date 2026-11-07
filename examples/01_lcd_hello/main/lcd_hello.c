/* 01_lcd_hello — display-stack example (skeleton in item 1).
 *
 * Item 1 proves the example build pattern only: it asks the BSP for the
 * display parts and heartbeats. Those parts are stubs until item 2, so
 * cb_bsp_init() is expected to log warnings and return ESP_ERR_NOT_SUPPORTED
 * (LCD and LVGL are the strict parts).
 *
 * Item 2 fills the TODO below with the diagnostic screen: colour bars plus
 * one-pixel markers at all four edges of the 172x320 panel — designed to
 * expose a missing x-gap (34), wrong colour order, or missing inversion the
 * moment a board is attached. Build-verified only until then.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "cb_bsp.h"

static const char *TAG = "lcd_hello";

void app_main(void)
{
    ESP_LOGI(TAG, "01_lcd_hello — display example (skeleton)");
    esp_err_t err = cb_bsp_init(CB_PART_BACKLIGHT | CB_PART_LCD | CB_PART_LVGL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cb_bsp_init(BACKLIGHT|LCD|LVGL) -> %s "
                 "(display stack lands in item 2)",
                 esp_err_to_name(err));
    }

    /* TODO(item2): colour bars + four edge markers (cb_lcd / LVGL) here. */

    for (int i = 1;; ++i) {
        ESP_LOGI(TAG, "waiting for display stack (item 2) #%d", i);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
