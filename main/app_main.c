/* hello-beeper — root application of the child-beeper toolkit.
 *
 * Brings the board up through the cb_bsp board support package and prints a
 * heartbeat over the USB-Serial/JTAG console (the Type-C port; console selected
 * by CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG in config/sdkconfig.board.defaults).
 *
 * In item 1 cb_bsp is a skeleton: unimplemented parts log a warning and — for
 * the strict parts (LCD, LVGL) — contribute ESP_ERR_NOT_SUPPORTED to the
 * return value. cb_bsp_init() never aborts the boot, so this app runs on a
 * half-populated BSP and comes alive as items 2-5 land, without changes here.
 *
 * Build-verified only until a board is attached (see docs/verification.md).
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"

#include "cb_bsp.h"

static const char *TAG = "hello_beeper";

void app_main(void)
{
    ESP_LOGI(TAG, "hello beeper — child-beeper toolkit root app");
    ESP_LOGI(TAG, "cb_bsp %s / ESP-IDF %s", cb_bsp_version(), esp_get_idf_version());

    /* Request every part. Item-1 expectation: the strict, not-yet-implemented
     * parts (LCD, LVGL) make this return ESP_ERR_NOT_SUPPORTED while every
     * part is still attempted and logged — see the WARN lines above. */
    esp_err_t err = cb_bsp_init(CB_PART_ALL);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "cb_bsp_init(CB_PART_ALL): all requested parts are up");
    } else {
        ESP_LOGW(TAG, "cb_bsp_init(CB_PART_ALL) -> %s "
                 "(expected while the BSP is a skeleton; see the warnings above)",
                 esp_err_to_name(err));
    }

    for (int i = 1;; ++i) {
        ESP_LOGI(TAG, "alive #%d", i);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
