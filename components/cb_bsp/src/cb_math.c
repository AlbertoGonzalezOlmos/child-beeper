#include "cb_math.h"

int cb_math_duty_from_percent(int percent, int max_duty)
{
    if (max_duty <= 0 || percent <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return max_duty;
    }
    return (int)(((int64_t)percent * (int64_t)max_duty) / 100);
}

int16_t cb_math_le_i16(const uint8_t *buf)
{
    uint16_t raw = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    int32_t decoded = raw >= UINT16_C(0x8000)
                          ? (int32_t)raw - INT32_C(0x10000)
                          : (int32_t)raw;
    return (int16_t)decoded;
}

float cb_math_accel_lsb_to_g(int16_t raw, unsigned range_g)
{
    return (float)raw * (float)range_g / 32768.0f;
}

float cb_math_gyro_lsb_to_dps(int16_t raw, unsigned range_dps)
{
    return (float)raw * (float)range_dps / 32768.0f;
}

float cb_math_qmi_temp_c(uint16_t raw)
{
    int32_t signed_raw = raw >= UINT16_C(0x8000)
                             ? (int32_t)raw - INT32_C(0x10000)
                             : (int32_t)raw;
    return (float)signed_raw / 256.0f;
}

float cb_math_adc_raw_to_mv(int raw, int cali_mv, bool calibrated)
{
    if (calibrated) {
        return (float)cali_mv;
    }
    if (raw < 0) {
        raw = 0;
    } else if (raw > 4095) {
        raw = 4095;
    }
    return (float)raw * 3300.0f / 4095.0f;
}
