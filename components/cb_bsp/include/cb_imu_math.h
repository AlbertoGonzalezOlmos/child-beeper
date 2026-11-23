#ifndef CB_IMU_MATH_H
#define CB_IMU_MATH_H

/*
 * cb_imu_math.h — pure IMU decode math (item 4), no IDF includes.
 * Owner: deepseek-pro (CONTRACT file-ownership). Host-compiled and
 * Arduino-shared (item 7 mirrors this header, R25 of plan.md).
 *
 * Frozen contract (ITEM4-RULINGS R16/R17): the raw buffer is the 14-byte
 * burst starting at CB_QMI_TEMP_L (0x33) and running through CB_QMI_GZ_H
 * (0x40); slot order TEMP, AX, AY, AZ, GX, GY, GZ at byte offsets
 * 0, 2, 4, 6, 8, 10, 12, little-endian int16. Accel sensitivity
 * 32768/range_g LSB per g; gyro 32768/range_dps LSB per dps; temperature
 * 1/256 degC per LSB (int16 two's complement). The decode has no length
 * parameter: the caller guarantees CB_IMU_RAW_LEN readable bytes.
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CB_QMI_TEMP_L (0x33) .. CB_QMI_GZ_H (0x40) inclusive (R16). */
#define CB_IMU_RAW_LEN 14

/* R9: floats, this exact field order, no timestamp, no temp function. */
typedef struct {
    float ax_g;       /* X acceleration, g          */
    float ay_g;       /* Y acceleration, g          */
    float az_g;       /* Z acceleration, g          */
    float gx_dps;     /* X angular rate, degrees/s  */
    float gy_dps;     /* Y angular rate, degrees/s  */
    float gz_dps;     /* Z angular rate, degrees/s  */
    float temp_c;     /* die temperature, degrees C */
} cb_imu_sample_t;

/* R9/R17: decodes the 14-byte burst into *out. Returns false and leaves *out
 * byte-identical when raw or out is NULL, accel_range_g is not in
 * {2,4,8,16}, or gyro_range_dps is not in {16,32,64,128,256,512,1024}.
 * INT16_MIN inputs are handled without signed-arithmetic negation (no UB). */
bool cb_imu_math_decode(const uint8_t *raw, unsigned accel_range_g,
                        unsigned gyro_range_dps, cb_imu_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CB_IMU_MATH_H */
