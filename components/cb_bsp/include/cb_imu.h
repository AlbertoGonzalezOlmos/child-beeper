#ifndef CB_IMU_H
#define CB_IMU_H

/*
 * cb_imu.h — QMI8658 6-axis IMU over the cb_i2c master bus (item 4).
 * Owner: deepseek-pro (CONTRACT file-ownership). Frozen R9 surface, exactly:
 * init/read/addr/who_am_i — no timestamp field, no separate temperature call,
 * no runtime range switching. The sample struct lives in the pure
 * cb_imu_math.h (host-tested, Arduino-shared). Fixed ranges +-8 g / +-512 dps
 * and the ODR come from the frozen R11/R18 configuration; the 14-byte burst
 * and decode contract are frozen in cb_imu_math.h (R16/R17).
 */
#include "cb_imu_math.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent; optional part. Requires a successful cb_i2c_init() first —
 * ESP_ERR_INVALID_STATE without a bus (cb_lcd_init_lvgl precedent). Frozen
 * init sequence (R21): probe 0x6A then 0x6B (logs each outcome) -> WHO_AM_I
 * == 0x05 (else WARN + error, never an abort) -> CTRL1 = ADDR_AI -> CTRL9 =
 * AHB_CLOCK_GATING (0x12, bounded completion wait — hardware fix 2026-11-23)
 * -> CTRL2 (+-8 g, ODR) -> CTRL3 (+-512 dps, ODR) -> CTRL5 (LPF mode 0) ->
 * CTRL7 = ACC_EN | GYR_EN -> register readback -> `up addr=.. who=..
 * range=8g/512dps odr=..Hz`. */
esp_err_t cb_imu_init(void);

/* Reads the 14-byte burst and decodes all seven fields (R22): ESP_OK even
 * for a noisy sample; ESP_ERR_INVALID_STATE before init; ESP_ERR_TIMEOUT
 * when the internal mutex is busy; bus errors are returned with *out
 * untouched. No per-read data lines from the driver (R22/R27). */
esp_err_t cb_imu_read(cb_imu_sample_t *out);

/* The 7-bit address that answered the probe: 0 before a fully successful
 * init, otherwise 0x6A or 0x6B. */
int cb_imu_addr(void);

/* Cached WHO_AM_I byte (0 until read); meaningful when cb_imu_addr() != 0. */
uint8_t cb_imu_who_am_i(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_IMU_H */
