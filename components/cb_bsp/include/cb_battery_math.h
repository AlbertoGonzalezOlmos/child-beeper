/*
 * cb_battery_math.h — battery voltage conversion + plausibility, item 3.
 * Owner: deepseek-pro (CONTRACT file-ownership). Pure C, no ESP-IDF/
 * FreeRTOS includes: host-tested and shared with the Arduino tree (item 7).
 *
 * Frozen semantics (plan.md R6): integer battery millivolts, int64
 * intermediates, negative input clamps to 0, truncating division, and NO
 * /1000 — the result is millivolts, the volts conversion belongs to the
 * caller's formatting. The plausibility window is a warning heuristic for
 * the missing-cell case (USB-only power), not a fuel gauge.
 */
#ifndef CB_BATTERY_MATH_H
#define CB_BATTERY_MATH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CB_BAT_PLAUSIBLE_MIN_MV 2500
#define CB_BAT_PLAUSIBLE_MAX_MV 4500

/**
 * @brief Battery millivolts from the ADC-pin millivolts:
 *        pin_mv * CB_BAT_DIVIDER_NUM * CB_BAT_CAL_GAIN_NUM /
 *        (CB_BAT_DIVIDER_DEN * CB_BAT_CAL_GAIN_DEN) with int64 intermediate
 *        and truncating division (3 x 700/695). Inputs <= 0 return 0; the
 *        result saturates at INT32_MAX.
 */
int32_t cb_battery_math_mv_from_pin_mv(int32_t pin_mv);

/**
 * @brief True when @p battery_mv is inside the inclusive plausibility
 *        window [CB_BAT_PLAUSIBLE_MIN_MV, CB_BAT_PLAUSIBLE_MAX_MV].
 *        Heuristic only (no fuel gauge, no cell-detect line).
 */
bool cb_battery_math_plausible(int32_t battery_mv);

#ifdef __cplusplus
}
#endif

#endif /* CB_BATTERY_MATH_H */
