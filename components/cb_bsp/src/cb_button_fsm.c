#include "cb_button_fsm.h"

void cb_btn_fsm_init(cb_btn_fsm_t *fsm)
{
    cb_btn_fsm_t zero = {
        false, false, false, false, false, 0u, 0u, 0u
    };
    *fsm = zero;
}

cb_btn_event_t cb_btn_fsm_update(cb_btn_fsm_t *fsm, bool level_pressed, uint32_t now_ms)
{
    bool was_stable = fsm->stable_pressed;

    if (level_pressed != fsm->raw_pressed) {
        fsm->raw_pressed = level_pressed;
        fsm->last_change_ms = now_ms;
    }

    if (fsm->raw_pressed != fsm->stable_pressed &&
        (uint32_t)(now_ms - fsm->last_change_ms) >= CB_BTN_DEBOUNCE_MS) {
        fsm->stable_pressed = fsm->raw_pressed;
    }

    if (fsm->stable_pressed && !was_stable) {
        fsm->press_start_ms = now_ms;
        fsm->long_fired = false;
        /* R4 erratum: keep click_pending across the second press. Clearing it
         * here makes the second-release double-click branch unreachable. */
        return CB_BTN_EV_PRESS;
    }

    if (fsm->stable_pressed && !fsm->long_fired &&
        (uint32_t)(now_ms - fsm->press_start_ms) >= CB_BTN_LONG_MS) {
        fsm->long_fired = true;
        fsm->click_pending = false;
        return CB_BTN_EV_LONG_PRESS;
    }

    if (!fsm->stable_pressed && was_stable) {
        if (fsm->long_fired) {
            fsm->long_fired = false;
            return CB_BTN_EV_RELEASE;
        }
        if (fsm->click_pending &&
            (uint32_t)(now_ms - fsm->click_ms) <= CB_BTN_DOUBLE_GAP_MS) {
            fsm->click_pending = false;
            fsm->dbl_pending = true;
            return CB_BTN_EV_RELEASE;
        }
        fsm->click_pending = true;
        fsm->click_ms = now_ms;
        return CB_BTN_EV_RELEASE;
    }

    if (fsm->click_pending &&
        (uint32_t)(now_ms - fsm->click_ms) >= CB_BTN_DOUBLE_GAP_MS) {
        fsm->click_pending = false;
        return CB_BTN_EV_CLICK;
    }

    if (fsm->dbl_pending) {
        fsm->dbl_pending = false;
        return CB_BTN_EV_DOUBLE_CLICK;
    }

    return CB_BTN_EV_NONE;
}
