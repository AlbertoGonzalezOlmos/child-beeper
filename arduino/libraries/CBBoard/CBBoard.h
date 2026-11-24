/*
 * CBBoard.h — umbrella-only Arduino access to the canonical pure modules (item 7, AR15).
 *
 * The mirror headers named below are NOT committed here: `./cb arduino` stages byte-equal
 * copies from components/cb_bsp/ into this library's src/ (see mirror.txt); pin-drift
 * enforces the byte-equality at every verify. No logic and no forked copies in this file
 * (AR4/AR15) — sketches use core Arduino APIs plus these shared pure functions.
 *
 * IDF -> Arduino mapping (docs/arduino.md holds the full table):
 *   cb_rgb_set(r,g,b)        -> cb_rgb_math_dim() + core rgbLedWrite(CB_PIN_RGB, r, g, b)
 *                               (dim cap CB_RGB_DIM_CAP_PERCENT is applied by the sketch)
 *   cb_button poller/events  -> pinMode(CB_PIN_BOOT_BTN, INPUT_PULLUP), digitalRead (active
 *                               low) fed into cb_btn_fsm_update() from loop() with millis()
 *   cb_battery_read(sample)  -> analogReadMilliVolts(CB_PIN_BAT_ADC) + cb_battery_math_*()
 *                               (mv_from_pin_mv / plausible; no IDF-only raw/cal fields)
 *   cb_imu_read(sample)      -> Wire at CB_PIN_I2C_SDA/SCL + CB_QMI_* registers +
 *                               cb_imu_math_decode() on the CB_IMU_RAW_LEN burst
 * Core-3.x PWM style only: ledcAttach + ledcWrite; the 2.x-era setup/attach calls are gone
 * and banned; SD mounts must never auto-format on failure.
 */
#ifndef CBBOARD_H
#define CBBOARD_H

#include "cb_pins.h"
#include "cb_math.h"
#include "cb_button_fsm.h"
#include "cb_qmi8658_regs.h"
#include "cb_rgb_math.h"
#include "cb_battery_math.h"
#include "cb_imu_math.h"

#endif /* CBBOARD_H */
