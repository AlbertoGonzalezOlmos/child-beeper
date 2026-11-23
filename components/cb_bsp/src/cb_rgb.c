/*
 * cb_rgb.c — single WS2812-compatible RGB LED (GRB) on CB_PIN_RGB, item 3.
 * Owner: deepseek-pro (CONTRACT file-ownership). led_strip 3.1.1 RMT
 * backend at 10 MHz; every channel is dimmed by cb_rgb_math_dim
 * (peak-scale to CB_RGB_DIM_CAP_PERCENT, ruling R1b) and the set+refresh
 * transaction is guarded by one mutex (led_strip is not re-entrant; bounded
 * 100 ms wait, ruling R4b). Contract: never aborts — every failure is
 * returned or logged, no ESP_ERROR_CHECK anywhere (optional part).
 */
#include "cb_rgb.h"

#include <stdbool.h>
#include <stdint.h>

#include "cb_pins.h"
#include "cb_rgb_math.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"

static const char *TAG = "cb_rgb";

static led_strip_handle_t s_strip;   /* NULL until init succeeded */
static SemaphoreHandle_t s_mutex;    /* NULL until init succeeded */

/* R14b: gated-WARN state, shared by several tasks — every check-and-update
 * happens under s_log_lock; the caller logs outside it. */
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;
static TickType_t s_last_busy_log;      /* busy-mutex WARN, at most 1/s */
static TickType_t s_last_set_fail_log;  /* set-failed WARN, at most 1/s */
static bool s_warned_no_init;           /* pre-init WARN, one-shot */

/* Rate limit (≤1 Hz): decide under the lock, log outside it. */
static bool rate_limited(TickType_t *last)
{
    TickType_t now = xTaskGetTickCount();
    bool allow;

    portENTER_CRITICAL(&s_log_lock);
    allow = ((TickType_t)(now - *last) >= pdMS_TO_TICKS(1000));
    if (allow) {
        *last = now;
    }
    portEXIT_CRITICAL(&s_log_lock);
    return allow;
}

/* One-shot gate for a permanent state (cleared again by cb_rgb_init). */
static bool one_shot(bool *done)
{
    bool allow;

    portENTER_CRITICAL(&s_log_lock);
    allow = !*done;
    *done = true;
    portEXIT_CRITICAL(&s_log_lock);
    return allow;
}

esp_err_t cb_rgb_init(void)
{
    if (s_strip != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGW(TAG, "no memory for mutex");
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = CB_PIN_RGB,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = { .with_dma = false },
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rmt init failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_strip = NULL;
        return err;
    }

    /* R10b: boot dark — clear through the same set+refresh path. */
    err = cb_rgb_off();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "clear failed: %s", esp_err_to_name(err));
        (void)led_strip_del(s_strip);
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_strip = NULL;
        return err;
    }

    ESP_LOGI(TAG, "rmt up gpio%d, cap %u%%", CB_PIN_RGB, CB_RGB_DIM_CAP_PERCENT);

    /* The part is up again: re-arm the pre-init one-shot WARN. */
    portENTER_CRITICAL(&s_log_lock);
    s_warned_no_init = false;
    portEXIT_CRITICAL(&s_log_lock);
    return ESP_OK;
}

esp_err_t cb_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t err;

    if (s_strip == NULL || s_mutex == NULL) {
        if (one_shot(&s_warned_no_init)) {
            ESP_LOGW(TAG, "not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    cb_rgb_math_dim(&r, &g, &b);

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (rate_limited(&s_last_busy_log)) {
            ESP_LOGW(TAG, "busy: refresh in progress");
        }
        return ESP_ERR_TIMEOUT;
    }
    err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        if (rate_limited(&s_last_set_fail_log)) {
            ESP_LOGW(TAG, "set failed: %s", esp_err_to_name(err));
        }
        return err;
    }
    ESP_LOGI(TAG, "set r=%u g=%u b=%u", (unsigned)r, (unsigned)g, (unsigned)b);
    return ESP_OK;
}

esp_err_t cb_rgb_off(void)
{
    return cb_rgb_set(0, 0, 0);
}
