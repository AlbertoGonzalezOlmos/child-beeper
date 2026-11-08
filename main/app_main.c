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
 * Boot summary: the USB host only re-attaches the serial console ~1 s after
 * reset (measured on this board), so the early boot logs — banner, the per-part
 * warnings, the init result — are usually lost. After the FIRST heartbeat,
 * once the console is observable, a compact one-shot summary is printed:
 * reset reason, cb_bsp version, the stored cb_bsp_init(CB_PART_ALL) result and
 * the IDF version.
 *
 * Build-verified only until a board is attached (see docs/verification.md).
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"

#include "cb_bsp.h"

static const char *TAG = "hello_beeper";

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
        if (i == 1) {
            /* One-shot, after the first heartbeat: the console is reliably
             * attached by now, so the boot state is observable even though
             * the early lines above were lost to the ~1 s re-attach delay. */
            ESP_LOGI(TAG,
                     "boot summary: reset=%s cb_bsp=%s init(CB_PART_ALL)=%s idf=%s",
                     reset_reason_name(esp_reset_reason()),
                     cb_bsp_version(),
                     esp_err_to_name(err),
                     esp_get_idf_version());
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
