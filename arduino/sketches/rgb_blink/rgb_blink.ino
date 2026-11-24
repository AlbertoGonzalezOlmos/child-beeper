/*
 * rgb_blink — item-7 Arduino parity sketch (AR16).
 *
 * RGB colour cycle on the WS2812 (GPIO38) using the shared dim math: every full-scale
 * primary tuple is passed through cb_rgb_math_dim() (R1b: hue-preserving peak cap,
 * CB_RGB_DIM_CAP_PERCENT -> peak 51) before rgbLedWrite(); the printed r/g/b values are
 * the post-dim values actually sent, matching the IDF 02_rgb_button contract semantics.
 * Never the deprecated core-2.x neopixel write call — it is a forbidden construct.
 *
 * Serial contract (AR16, byte-exact), cycle step every 2 s, alive every 5 s:
 *   rgb_blink: cycle %u r=%u g=%u b=%u
 *   rgb_blink: alive #%u
 */
#include <CBBoard.h>

static const unsigned long CYCLE_MS = 2000; /* AR16: one colour step every 2 s */
static const unsigned long ALIVE_MS = 5000; /* AR16: alive heartbeat every 5 s */

static unsigned s_cycle = 0; /* monotonic cycle counter */
static unsigned s_alive = 0; /* monotonic alive counter */
static unsigned long s_next_cycle = 0;
static unsigned long s_next_alive = 0;

void setup() {
  Serial.begin(115200); /* FQBN CDCOnBoot=cdc -> Type-C console */
  /* Bounded wait for the USB-CDC host; never block forever. */
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 1500) {
  }
  s_next_cycle = millis();
  s_next_alive = millis() + ALIVE_MS;
}

void loop() {
  unsigned long now = millis();

  /* Wrap-safe deadline compares. */
  if ((long)(now - s_next_cycle) >= 0) {
    s_next_cycle += CYCLE_MS;
    ++s_cycle;
    /* 5 full-scale steps: red, green, blue, white, off. */
    uint8_t r = 0, g = 0, b = 0;
    switch ((s_cycle - 1) % 5) {
      case 0: r = 255; break;
      case 1: g = 255; break;
      case 2: b = 255; break;
      case 3: r = 255; g = 255; b = 255; break;
      default: break;
    }
    cb_rgb_math_dim(&r, &g, &b); /* shared dim cap (idempotent, hue-preserving) */
    rgbLedWrite(CB_PIN_RGB, r, g, b);
    Serial.printf("rgb_blink: cycle %u r=%u g=%u b=%u\n", s_cycle, r, g, b);
  }

  if ((long)(now - s_next_alive) >= 0) {
    s_next_alive += ALIVE_MS;
    ++s_alive;
    Serial.printf("rgb_blink: alive #%u\n", s_alive);
  }
}
