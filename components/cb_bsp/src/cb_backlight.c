/*
 * cb_backlight.c — LCD backlight PWM, item 2. Owner: mimo26-pro (CONTRACT
 * file-ownership). Wiring uses frozen cb_pins.h macros only (rule: no GPIO
 * literals). Contract: never aborts the process — every failure is returned or
 * logged, no ESP_ERROR_CHECK anywhere.
 *
 * Vendor caveat (hardware-reference.md): do not touch the vendor's duty-1024
 * bug; 100% is cb_math_duty_from_percent(100, CB_MATH_BACKLIGHT_MAX) = 1023.
 */
#include "cb_backlight.h"

#include "cb_math.h"
#include "cb_pins.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "cb_backlight";

static bool s_inited;
static int  s_percent;
static bool s_warned;

esp_err_t cb_backlight_init(void)
{
    if (s_inited) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    /* PWM: CB_LCD_BL_PWM_HZ, 10-bit resolution, LOW_SPEED (no ISR). Start at
     * duty 0 so there is no white flash before LVGL's first frame (ruling 2). */
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = CB_LCD_BL_PWM_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_timer_config: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel = {
        .gpio_num   = CB_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc_channel_config: %s", esp_err_to_name(err));
        (void)ledc_timer_rst(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0);
        return err;
    }

    s_inited = true;
    s_percent = 0;
    ESP_LOGI(TAG, "backlight on GPIO%d, %u Hz, duty 0 (off)",
             CB_PIN_LCD_BL, (unsigned)CB_LCD_BL_PWM_HZ);
    return ESP_OK;
}

void cb_backlight_set(int percent)
{
    if (!s_inited) {
        if (!s_warned) {
            ESP_LOGW(TAG, "set(%d) before init: ignored", percent);
            s_warned = true;
        }
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = cb_math_duty_from_percent(percent, CB_MATH_BACKLIGHT_MAX);
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set(%d%%) duty %lu: %s", percent, (unsigned long)duty,
                 esp_err_to_name(err));
        return;
    }
    s_percent = percent;
}

int cb_backlight_get(void)
{
    return s_percent;
}
