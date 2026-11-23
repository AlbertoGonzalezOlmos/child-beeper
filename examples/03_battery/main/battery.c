/*
 * 03_battery — battery ADC diagnostic example (toolkit item 3).
 *
 * Exercises the optional cb_battery part (ADC1_CH0 oneshot on GPIO1, 12 dB
 * atten) through cb_bsp: one cb_battery_read() every 2 s prints the full
 * sample. The 3x divider (200k/100k) and the 700/695 eFuse gain are applied
 * inside the BSP; `plausible` is the 2500..4500 mV heuristic (a warning
 * heuristic, NOT cell detection or a state-of-charge estimate).
 *
 * Serial contract (frozen strings — also quoted in README.md):
 *   I (…) battery: ready
 *   I (…) battery: raw=%d pin_mv=%ld battery_mv=%ld calibrated=%d plausible=%d  (every 2 s)
 *   I (…) battery: alive #%u                                                    (every 5 s)
 *   W (…) battery: read failed: %s                    (R13b: one-shot, then polling stops)
 *
 * The transition-gated no-cell WARN is owned by the driver (R6b):
 *   W (…) cb_battery: implausible %ld mV (no cell inserted?)
 *   I (…) cb_battery: reading plausible again
 * This example prints data lines only.
 */
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cb_bsp.h"
#include "cb_battery.h"

static const char *TAG = "battery";

#define SAMPLE_PERIOD_MS 2000   /* one sample every 2 s */
#define ALIVE_PERIOD_US  5000000LL  /* heartbeat every 5 s */

void app_main(void)
{
    /* BATTERY is an optional part: cb_bsp_init suppresses its failure with a
     * WARN of its own and returns ESP_OK; a dead part then shows up as sample
     * lines simply not appearing, with the alive heartbeat staying up. */
    (void)cb_bsp_init(CB_PART_BATTERY);

    /* Frozen contract: printed as soon as init returns — no artificial delay. */
    ESP_LOGI(TAG, "ready");

    unsigned alive = 0;
    bool polling = true;
    int64_t next_alive_us = esp_timer_get_time() + ALIVE_PERIOD_US; /* first alive at +5 s */
    for (;;) {
        if (polling) {
            cb_battery_sample_t s;
            esp_err_t err = cb_battery_read(&s);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "raw=%d pin_mv=%ld battery_mv=%ld calibrated=%d plausible=%d",
                         s.raw, (long)s.pin_mv, (long)s.battery_mv,
                         (int)s.calibrated, (int)s.plausible);
            } else {
                /* R13b: one-shot frozen line naming the cause, then never
                 * hammer a dead part (R7: no WARN spam) — the alive heartbeat
                 * carries the contract on. */
                ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(err));
                polling = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));

        int64_t now_us = esp_timer_get_time();
        if (now_us >= next_alive_us) {
            next_alive_us = now_us + ALIVE_PERIOD_US;
            ESP_LOGI(TAG, "alive #%u", ++alive);
        }
    }
}
