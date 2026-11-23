#ifndef CB_I2C_H
#define CB_I2C_H

/*
 * cb_i2c.h — shared I2C master bus (item 4).
 * Owner: deepseek-pro (CONTRACT file-ownership). Frozen surface (ITEM4-RULINGS
 * R15): exactly these two symbols — no probe helper (R14), no recovery/reset
 * API in item 4. CB_PART_I2C owns the bus (k_parts[] order: I2C before IMU);
 * cb_imu_init() returns ESP_ERR_INVALID_STATE while cb_i2c_bus() is NULL.
 * The bus is never deleted by device drivers; the i2c_master driver
 * serializes transactions on it. This header includes driver/i2c_master.h,
 * so esp_driver_i2c is a PUBLIC REQUIRES of cb_bsp (R13).
 */
#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent; optional part. Creates the one master bus on CB_I2C_PORT with
 * internal pull-ups enabled (R12) and logs the frozen line
 * `I i2c: internal pull-ups enabled`. Failed init marks nothing: retry. */
esp_err_t cb_i2c_init(void);

/* The shared bus handle, NULL until cb_i2c_init() succeeds. */
i2c_master_bus_handle_t cb_i2c_bus(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_I2C_H */
