#ifndef CB_BUTTON_FSM_H
#define CB_BUTTON_FSM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CB_BTN_EV_NONE = 0,
    CB_BTN_EV_PRESS,
    CB_BTN_EV_RELEASE,
    CB_BTN_EV_CLICK,
    CB_BTN_EV_DOUBLE_CLICK,
    CB_BTN_EV_LONG_PRESS
} cb_btn_event_t;

#define CB_BTN_DEBOUNCE_MS 20
#define CB_BTN_DOUBLE_GAP_MS 350
#define CB_BTN_LONG_MS 800

typedef struct {
    /* Private-ish fields: use cb_btn_fsm_init/update, do not inspect directly. */
    bool raw_pressed;
    bool stable_pressed;
    bool long_fired;
    bool click_pending;
    bool dbl_pending;
    uint32_t last_change_ms;
    uint32_t press_start_ms;
    uint32_t click_ms;
} cb_btn_fsm_t;

void cb_btn_fsm_init(cb_btn_fsm_t *fsm);
cb_btn_event_t cb_btn_fsm_update(cb_btn_fsm_t *fsm, bool level_pressed, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* CB_BUTTON_FSM_H */
