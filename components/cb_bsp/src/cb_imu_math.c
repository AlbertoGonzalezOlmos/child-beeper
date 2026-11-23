/*
 * cb_imu_math.c — pure IMU decode math (item 4), host-tested.
 * Owner: deepseek-pro (CONTRACT file-ownership).
 *
 * Implements exactly the frozen R17 contract; every conversion goes through
 * the frozen cb_math helpers (le_i16, accel/gyro LSB scaling, QMI8658
 * temperature) — this module re-derives no register layout or sensitivity
 * constant (R10). Invalid inputs leave *out untouched.
 */
#include "cb_imu_math.h"

#include <stddef.h>

#include "cb_math.h"

static bool accel_range_valid(unsigned range_g)
{
    switch (range_g) {
    case 2:
    case 4:
    case 8:
    case 16:
        return true;
    default:
        return false;
    }
}

static bool gyro_range_valid(unsigned range_dps)
{
    switch (range_dps) {
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
    case 512:
    case 1024:
        return true;
    default:
        return false;
    }
}

bool cb_imu_math_decode(const uint8_t *raw, unsigned accel_range_g,
                        unsigned gyro_range_dps, cb_imu_sample_t *out)
{
    cb_imu_sample_t sample;
    uint16_t temp_raw;

    if (raw == NULL || out == NULL) {
        return false;
    }
    if (!accel_range_valid(accel_range_g) || !gyro_range_valid(gyro_range_dps)) {
        return false;
    }

    /* Burst slots (R17): temp [0..1], accel [2..7], gyro [8..13]. */
    temp_raw = (uint16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    sample.temp_c = cb_math_qmi_temp_c(temp_raw);

    sample.ax_g = cb_math_accel_lsb_to_g(cb_math_le_i16(&raw[2]), accel_range_g);
    sample.ay_g = cb_math_accel_lsb_to_g(cb_math_le_i16(&raw[4]), accel_range_g);
    sample.az_g = cb_math_accel_lsb_to_g(cb_math_le_i16(&raw[6]), accel_range_g);
    sample.gx_dps = cb_math_gyro_lsb_to_dps(cb_math_le_i16(&raw[8]), gyro_range_dps);
    sample.gy_dps = cb_math_gyro_lsb_to_dps(cb_math_le_i16(&raw[10]), gyro_range_dps);
    sample.gz_dps = cb_math_gyro_lsb_to_dps(cb_math_le_i16(&raw[12]), gyro_range_dps);

    /* Single write at the very end: a failure can never half-write. */
    *out = sample;
    return true;
}
