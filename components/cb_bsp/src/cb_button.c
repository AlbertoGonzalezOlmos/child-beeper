/*
 * cb_button.c — BOOT button wrapper over the host-tested cb_button_fsm,
 * item 3. Owner: deepseek-pro (CONTRACT file-ownership). GPIO0 (strapping
 * pin) is configured INPUT + pull-up and never driven (download mode stays
 * reachable); one esp_timer polls every CB_BUTTON_POLL_MS and feeds the
 * frozen FSM — the wrapper adds no second debounce/gesture logic. Callback
 * dispatch (ruling R5b): the (cb, user) pair is swapped under a critical
 * section and the callback is invoked outside it, from the esp_timer task
 * (never ISR). Contract: never aborts (optional part).
 */
#include "cb_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "cb_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "cb_button";

static cb_btn_fsm_t s_fsm;
static esp_timer_handle_t s_timer;   /* NULL until created */
static cb_button_cb_t s_cb;          /* NULL = silent */
static void *s_user;
static portMUX_TYPE s_cb_lock = portMUX_INITIALIZER_UNLOCKED;

static void timer_cb(void *arg)
{
    (void)arg;
    bool pressed = (gpio_get_level(CB_PIN_BOOT_BTN) == 0);   /* active low */
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    cb_btn_event_t event = cb_btn_fsm_update(&s_fsm, pressed, now_ms);

    if (event != CB_BTN_EV_NONE) {
        cb_button_cb_t cb;
        void *user;

        portENTER_CRITICAL(&s_cb_lock);
        cb = s_cb;
        user = s_user;
        portEXIT_CRITICAL(&s_cb_lock);

        if (cb != NULL) {
            cb(event, user);
        }
    }
}

esp_err_t cb_button_init(void)
{
    if (s_timer != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    /* GPIO0 is a strapping pin: input + pull-up only, never driven. */
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CB_PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio config failed: %s", esp_err_to_name(err));
        return err;
    }

    cb_btn_fsm_init(&s_fsm);
    /* Seed with the resting level (single-threaded init, timer not yet
     * running — the only FSM mutation outside the timer callback). */
    (void)cb_btn_fsm_update(&s_fsm,
                            gpio_get_level(CB_PIN_BOOT_BTN) == 0,
                            (uint32_t)(esp_timer_get_time() / 1000ULL));

    esp_timer_create_args_t args = {
        .callback = timer_cb,
        .arg = NULL,
        .name = "cb_btn",
        .dispatch_method = ESP_TIMER_TASK,
    };
    err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer create failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_timer_start_periodic(s_timer, CB_BUTTON_POLL_MS * 1000);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "timer start failed: %s", esp_err_to_name(err));
        (void)esp_timer_delete(s_timer);
        s_timer = NULL;
        return err;
    }

    ESP_LOGI(TAG, "poller up, %d ms", CB_BUTTON_POLL_MS);
    return ESP_OK;
}

esp_err_t cb_button_set_callback(cb_button_cb_t cb, void *user)
{
    /* R5b: always stores the pair and returns ESP_OK; NULL disables. */
    portENTER_CRITICAL(&s_cb_lock);
    s_cb = cb;
    s_user = user;
    portEXIT_CRITICAL(&s_cb_lock);
    return ESP_OK;
}
