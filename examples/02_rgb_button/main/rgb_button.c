/*
 * 02_rgb_button — RGB LED + BOOT button diagnostic example (toolkit item 3).
 *
 * Exercises two optional BSP parts through cb_bsp:
 *   - cb_rgb: WS2812 (GRB) on GPIO38 via RMT; cb_rgb_set() applies the shared
 *     cb_rgb_math_dim() peak-cap (CB_RGB_DIM_CAP_PERCENT 20 => peak 51), so the
 *     values logged below are the values actually sent to the LED.
 *   - cb_button: BOOT button (GPIO0, input-only strapping pin) through the frozen
 *     cb_button_fsm + a 10 ms esp_timer poller. Events arrive in the esp_timer
 *     TASK context — the callback below only logs and sets a flag; all LED work
 *     happens in the app_main loop.
 *
 * Serial contract (frozen strings — also quoted in README.md):
 *   I (…) rgb_button: ready poll_ms=10 cap_percent=20
 *   I (…) rgb_button: cycle %u r=%u g=%u b=%u      (every 2 s + on CLICK)
 *   W (…) rgb_button: cycle %u failed: %s          (only on error)
 *   I (…) rgb_button: event %s                     (PRESS|RELEASE|CLICK|DOUBLE_CLICK|LONG_PRESS)
 *   I (…) rgb_button: alive #%u                    (every 5 s)
 *
 * The colour cycle is red, green, blue, white, off (5 steps, announced each),
 * repeating with a monotonic counter: steps 1..5 then 6..10, step N uses
 * k_steps[(N-1) % 5].
 */
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cb_bsp.h"
#include "cb_button.h"
#include "cb_rgb.h"
#include "cb_rgb_math.h"

static const char *TAG = "rgb_button";

#define CYCLE_PERIOD_US 2000000LL   /* one colour step every 2 s */
#define ALIVE_PERIOD_US 5000000LL   /* heartbeat every 5 s */
#define TICK_MS 50                  /* loop tick; keeps CLICK handling prompt */

/* Full-scale input colours; cb_rgb_math_dim() peak-caps each step to 51 before
 * it is sent and logged, e.g. red (255,0,0) -> (51,0,0). */
static const uint8_t k_steps[5][3] = {
    { 255,   0,   0 },  /* red   */
    {   0, 255,   0 },  /* green */
    {   0,   0, 255 },  /* blue  */
    { 255, 255, 255 },  /* white */
    {   0,   0,   0 },  /* off   */
};

static volatile bool s_click_pending; /* esp_timer task -> app_main (single flag) */
static unsigned s_cycle;              /* only ever written by app_main */
static unsigned s_alive;              /* only ever written by app_main */

static void step_cycle(void)
{
    /* Count first so a failing step still burns its number (no repeat lines). */
    unsigned n = ++s_cycle;
    uint8_t r = k_steps[(n - 1) % 5][0];
    uint8_t g = k_steps[(n - 1) % 5][1];
    uint8_t b = k_steps[(n - 1) % 5][2];

    cb_rgb_math_dim(&r, &g, &b);      /* values as sent (peak cap 51) */
    esp_err_t err = cb_rgb_set(r, g, b);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cycle %u failed: %s", n, esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "cycle %u r=%u g=%u b=%u", n, r, g, b);
}

/* Runs in the esp_timer task (cb_button.h contract): log + flag only.
 * No LED/SD/blocking work here; the app_main loop does the LED stepping. */
static void on_button(cb_btn_event_t event, void *user)
{
    (void)user;
    const char *name;

    switch (event) {
    case CB_BTN_EV_PRESS:        name = "PRESS";        break;
    case CB_BTN_EV_RELEASE:      name = "RELEASE";      break;
    case CB_BTN_EV_CLICK:        name = "CLICK";        break;
    case CB_BTN_EV_DOUBLE_CLICK: name = "DOUBLE_CLICK"; break;
    case CB_BTN_EV_LONG_PRESS:   name = "LONG_PRESS";   break;
    default:                     name = "UNKNOWN";      break;
    }
    ESP_LOGI(TAG, "event %s", name);

    if (event == CB_BTN_EV_CLICK) {
        s_click_pending = true;   /* advance the colour cycle early */
    }
}

void app_main(void)
{
    /* RGB and BUTTON are optional parts: cb_bsp_init suppresses their failures
     * with a WARN of its own and returns ESP_OK; the frozen `cycle %u failed`
     * line below exposes a dead LED, and events simply never arrive when the
     * button part is down. */
    (void)cb_bsp_init(CB_PART_RGB | CB_PART_BUTTON);
    (void)cb_button_set_callback(on_button, NULL); /* R5b: always ESP_OK */

    /* Frozen contract: printed as soon as init returns — no artificial delay. */
    ESP_LOGI(TAG, "ready poll_ms=10 cap_percent=20");

    int64_t next_cycle_us = esp_timer_get_time();
    int64_t next_alive_us = next_cycle_us + ALIVE_PERIOD_US; /* first alive at +5 s */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        int64_t now_us = esp_timer_get_time();

        bool click = s_click_pending;
        s_click_pending = false;
        if (click || now_us >= next_cycle_us) {
            next_cycle_us = now_us + CYCLE_PERIOD_US;
            step_cycle();
        }
        if (now_us >= next_alive_us) {
            next_alive_us = now_us + ALIVE_PERIOD_US;
            ESP_LOGI(TAG, "alive #%u", ++s_alive);
        }
    }
}
