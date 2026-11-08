/*
 * cb_bsp.h — Child Beeper board support package, public API (item 1: skeleton).
 *
 * Owner: mimo26-pro. Names/signatures/values below are frozen by CONTRACT.md;
 * change them only through the orchestrator's review round.
 *
 * Error policy (CONTRACT R5): strict parts = {LCD, LVGL}; every other part is
 * optional. cb_bsp_init() attempts every requested part in the frozen order
 * BACKLIGHT -> RGB -> I2C -> IMU -> BUTTON -> BATTERY -> LCD -> LVGL -> SD.
 * Optional failures are logged at WARN and suppressed; strict failures are
 * logged at ERROR and the first one is returned. A requested-but-not-yet-
 * implemented part logs "WARN ... not implemented" and, if strict, contributes
 * ESP_ERR_NOT_SUPPORTED to the return value. cb_bsp_init(0) == ESP_OK.
 */
#ifndef CB_BSP_H
#define CB_BSP_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CB_BSP_VERSION_MAJOR 0
#define CB_BSP_VERSION_MINOR 1
#define CB_BSP_VERSION_PATCH 0

typedef enum {
    CB_PART_NONE      = 0,
    CB_PART_BACKLIGHT = 1u << 0,
    CB_PART_RGB       = 1u << 1,
    CB_PART_I2C       = 1u << 2,
    CB_PART_IMU       = 1u << 3,
    CB_PART_BUTTON    = 1u << 4,
    CB_PART_BATTERY   = 1u << 5,
    CB_PART_LCD       = 1u << 6,
    CB_PART_LVGL      = 1u << 7,
    CB_PART_SD        = 1u << 8,
    CB_PART_ALL       = 0x1FF,
} cb_part_t;

esp_err_t cb_bsp_init(uint32_t parts);   /* inits only the requested parts, in fixed order:
                                            BACKLIGHT -> RGB -> I2C -> IMU -> BUTTON -> BATTERY
                                            -> LCD -> LVGL -> SD (SD last: mount can block)      */
const char *cb_bsp_version(void);        /* "0.1.0" */

#ifdef __cplusplus
}
#endif

#endif /* CB_BSP_H */
