/*
 * cb_button.h — BOOT button wrapper around the host-tested cb_button_fsm,
 * item 3. Owner: deepseek-pro (CONTRACT file-ownership). GPIO0 (strapping
 * pin) is configured INPUT + pull-up and is never driven; one esp_timer
 * polls every CB_BUTTON_POLL_MS and feeds the frozen FSM — no second
 * debounce/gesture logic anywhere.
 */
#ifndef CB_BUTTON_H
#define CB_BUTTON_H

#include "cb_button_fsm.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CB_BUTTON_POLL_MS 10  /* poll period; within the ruled 5-10 ms band */

/**
 * @brief FSM event handler. Runs in the esp_timer TASK context (never ISR):
 *        must return promptly, must not block, and must only touch LVGL
 *        under cb_lcd_lvgl_lock(). At most one event is dispatched per poll
 *        (the FSM contract). @p user is the context passed to
 *        cb_button_set_callback().
 */
typedef void (*cb_button_cb_t)(cb_btn_event_t event, void *user);

/**
 * @brief Configure GPIO0 as a pulled-up input (never output), seed the FSM
 *        with the resting level, and start the CB_BUTTON_POLL_MS periodic
 *        esp_timer feeding cb_btn_fsm_update(). Idempotent; on failure the
 *        timer is torn down and the error returned (retry allowed). Never
 *        aborts (optional part).
 */
esp_err_t cb_button_init(void);

/**
 * @brief Register/replace the event handler. Always stores the pair and
 *        returns ESP_OK (ruling R5b); callable before or after
 *        cb_button_init(). NULL disables dispatch (silent mode). The pair
 *        is swapped under a critical section and the callback is invoked
 *        outside it.
 */
esp_err_t cb_button_set_callback(cb_button_cb_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* CB_BUTTON_H */
