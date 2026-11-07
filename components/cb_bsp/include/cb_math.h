#ifndef CB_MATH_H
#define CB_MATH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CB_MATH_BACKLIGHT_MAX 1023 /* 10-bit LEDC */

int cb_math_duty_from_percent(int percent, int max_duty);
int16_t cb_math_le_i16(const uint8_t *buf);
float cb_math_accel_lsb_to_g(int16_t raw, unsigned range_g);
float cb_math_gyro_lsb_to_dps(int16_t raw, unsigned range_dps);
float cb_math_qmi_temp_c(uint16_t raw);
float cb_math_adc_raw_to_mv(int raw, int cali_mv, bool calibrated);

#ifdef __cplusplus
}
#endif

#endif /* CB_MATH_H */
