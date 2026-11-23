/*
 * 04_imu — QMI8658 IMU diagnostic example (toolkit item 4).
 *
 * Exercises the optional cb_imu part (QMI8658 on the shared I2C bus, probed
 * at 0x6A then 0x6B) through cb_bsp: one cb_imu_read() every 2 s prints the
 * full cb_imu_sample_t decoded from the frozen 14-byte TEMP..GZ burst. The
 * device runs at the fixed item-4 ranges +-8 g / +-512 dps; the ODR comes
 * from the cb_bsp Kconfig choice (default 120 Hz).
 *
 * Serial contract (frozen strings, R23 — also quoted in README.md):
 *   I (…) imu: ready addr=0x%02X who=0x%02X                                (once)
 *   I (…) imu: ax=%+.3f ay=%+.3f az=%+.3f gx=%+.2f gy=%+.2f gz=%+.2f temp_c=%+.2f
 *                                                                        (every 2 s)
 *   I (…) imu: alive #%u                                                  (every 5 s)
 *   W (…) imu: read failed: %s                    (R13b: one-shot, then polling stops)
 *   W (…) imu: no IMU found (probed 0x6A/0x6B), samples skipped  (once, when addr == 0)
 *
 * The probe/WHO WARNs and the pull-up/config init lines are owned by the
 * driver (R6b); this example prints data lines only and never aborts on
 * missing hardware.
 */
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cb_bsp.h"
#include "cb_imu.h"

static const char *TAG = "imu";

#define SAMPLE_PERIOD_MS 2000       /* one sample every 2 s */
#define ALIVE_PERIOD_US  5000000LL  /* heartbeat every 5 s */

void app_main(void)
{
    /* I2C and IMU are optional parts: cb_bsp_init suppresses their failures
     * with WARNs of its own and returns ESP_OK; dead hardware then shows up
     * as the no-IMU line below (or as stopped samples), with the alive
     * heartbeat staying up. */
    (void)cb_bsp_init(CB_PART_I2C | CB_PART_IMU);

    bool polling;
    int addr = cb_imu_addr();
    if (addr != 0) {
        /* Frozen contract: printed as soon as init returns — no artificial
         * delay. The line carries the probe evidence (which address answered,
         * WHO_AM_I) the hardware checklist greps for. */
        ESP_LOGI(TAG, "ready addr=0x%02X who=0x%02X",
                 (unsigned)addr, (unsigned)cb_imu_who_am_i());
        polling = true;
    } else {
        /* R23: one frozen line, then alive-only mode — never abort. */
        ESP_LOGW(TAG, "no IMU found (probed 0x6A/0x6B), samples skipped");
        polling = false;
    }

    unsigned alive = 0;
    int64_t next_alive_us = esp_timer_get_time() + ALIVE_PERIOD_US; /* first alive at +5 s */
    for (;;) {
        if (polling) {
            cb_imu_sample_t s;
            esp_err_t err = cb_imu_read(&s);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "ax=%+.3f ay=%+.3f az=%+.3f gx=%+.2f gy=%+.2f gz=%+.2f temp_c=%+.2f",
                         (double)s.ax_g, (double)s.ay_g, (double)s.az_g,
                         (double)s.gx_dps, (double)s.gy_dps, (double)s.gz_dps,
                         (double)s.temp_c);
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
