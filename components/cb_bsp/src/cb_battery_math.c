/*
 * cb_battery_math.c — battery voltage math, item 3. Owner: deepseek-pro
 * (CONTRACT file-ownership). Pure C, host-tested; semantics frozen by
 * plan.md R6 (int64 intermediates, negative -> 0, truncating, saturation).
 */
#include "cb_battery_math.h"

#include <limits.h>

#include "cb_pins.h"

int32_t cb_battery_math_mv_from_pin_mv(int32_t pin_mv)
{
    int64_t numerator;
    int64_t denominator;

    if (pin_mv <= 0) {
        return 0;
    }
    numerator = (int64_t)pin_mv * CB_BAT_DIVIDER_NUM * CB_BAT_CAL_GAIN_NUM;
    denominator = (int64_t)CB_BAT_DIVIDER_DEN * CB_BAT_CAL_GAIN_DEN;
    numerator /= denominator;               /* truncating */
    if (numerator > INT32_MAX) {
        return INT32_MAX;                   /* saturate: no signed overflow */
    }
    return (int32_t)numerator;
}

bool cb_battery_math_plausible(int32_t battery_mv)
{
    return battery_mv >= CB_BAT_PLAUSIBLE_MIN_MV &&
           battery_mv <= CB_BAT_PLAUSIBLE_MAX_MV;
}
