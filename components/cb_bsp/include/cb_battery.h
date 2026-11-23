/*
 * cb_battery.h — battery sense (ADC1 CH0 on CB_PIN_BAT_ADC), item 3.
 * Owner: deepseek-pro (CONTRACT file-ownership). Voltage only: no fuel
 * gauge, no charge-status GPIO. The divider (x3) and gain (x700/695)
 * conversion lives in the pure cb_battery_math module (frozen ruling: never
 * inside cb_math). Contract: never aborts (optional part).
 */
#ifndef CB_BATTERY_H
#define CB_BATTERY_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One battery sample.
 */
typedef struct {
    int     raw;          /* ADC oneshot raw */
    int32_t pin_mv;       /* calibrated ADC-pin millivolts (cb_math fallback if no eFuse cal) */
    int32_t battery_mv;   /* cb_battery_math_mv_from_pin_mv(pin_mv): divider x3, gain x700/695 */
    bool    calibrated;   /* curve-fitting calibration handle was usable (ruling R3b) */
    bool    plausible;    /* inside CB_BAT_PLAUSIBLE_MIN/MAX_MV */
} cb_battery_sample_t;

/**
 * @brief Bring up ADC1 CH0 oneshot at 12 dB with curve-fitting calibration
 *        (ruling R3b); when calibration cannot be created, WARN once and
 *        keep the linear fallback (cb_math_adc_raw_to_mv) — that is NOT an
 *        init failure. Only unit/channel setup failure fails the part.
 *        Idempotent; retryable after a failure; never aborts.
 */
esp_err_t cb_battery_init(void);

/**
 * @brief Take one sample. NULL @p out -> ESP_ERR_INVALID_ARG; before
 *        cb_battery_init() -> ESP_ERR_INVALID_STATE. On success all five
 *        fields are filled (an implausible reading still returns ESP_OK —
 *        data, not an error) and the implausible/recovered transition is
 *        logged once per change (ruling R6b). No per-read data line is
 *        printed — examples own the data lines. Reads are serialised by an
 *        internal mutex (100 ms bound).
 */
esp_err_t cb_battery_read(cb_battery_sample_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CB_BATTERY_H */
