/*
 * button_events — item-7 Arduino parity sketch (AR16).
 *
 * BOOT button (GPIO0, strapping pin: input+pull-up only, never driven) through the shared
 * cb_button_fsm: loop() polls digitalRead every POLL_MS and feeds cb_btn_fsm_update()
 * with millis(); the frozen FSM owns all debounce/gesture timing (no second debounce,
 * AR4/AR15). Event tokens are the frozen cb_btn_event_t names. Gestures need a human.
 *
 * Serial contract (AR16, byte-exact):
 *   button_events: ready poll_ms=%d      (once)
 *   button_events: button event %s       (per FSM event: PRESS RELEASE CLICK
 *                                         DOUBLE_CLICK LONG_PRESS)
 *   button_events: alive #%u             (every 5 s)
 */
#include <CBBoard.h>

/* == CB_BUTTON_POLL_MS of the IDF twin; that macro lives in cb_button.h, which is a
 * driver header and intentionally not part of the mirrored pure set. */
static const int POLL_MS = 10;
static const unsigned long ALIVE_MS = 5000; /* AR16: alive heartbeat every 5 s */

static cb_btn_fsm_t s_fsm;
static unsigned s_alive = 0;
static unsigned long s_next_poll = 0;
static unsigned long s_next_alive = 0;

static const char *event_name(cb_btn_event_t ev) {
  switch (ev) {
    case CB_BTN_EV_PRESS:        return "PRESS";
    case CB_BTN_EV_RELEASE:      return "RELEASE";
    case CB_BTN_EV_CLICK:        return "CLICK";
    case CB_BTN_EV_DOUBLE_CLICK: return "DOUBLE_CLICK";
    case CB_BTN_EV_LONG_PRESS:   return "LONG_PRESS";
    default:                     return "NONE";
  }
}

void setup() {
  Serial.begin(115200); /* FQBN CDCOnBoot=cdc -> Type-C console */
  /* Bounded wait for the USB-CDC host; never block forever. */
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 1500) {
  }
  pinMode(CB_PIN_BOOT_BTN, INPUT_PULLUP); /* GPIO0 active-low */
  cb_btn_fsm_init(&s_fsm);
  Serial.printf("button_events: ready poll_ms=%d\n", POLL_MS);
  s_next_poll = millis();
  s_next_alive = millis() + ALIVE_MS;
}

void loop() {
  unsigned long now = millis();

  /* Wrap-safe deadline compares. */
  if ((long)(now - s_next_poll) >= 0) {
    s_next_poll += POLL_MS;
    bool pressed = (digitalRead(CB_PIN_BOOT_BTN) == LOW);
    cb_btn_event_t ev = cb_btn_fsm_update(&s_fsm, pressed, (uint32_t)millis());
    if (ev != CB_BTN_EV_NONE) {
      Serial.printf("button_events: button event %s\n", event_name(ev));
    }
  }

  if ((long)(now - s_next_alive) >= 0) {
    s_next_alive += ALIVE_MS;
    ++s_alive;
    Serial.printf("button_events: alive #%u\n", s_alive);
  }
}
