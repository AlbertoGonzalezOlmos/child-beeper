/*
 * cb_battery.c — battery sense (ADC1 CH0 on CB_PIN_BAT_ADC), item 3.
 * Owner: deepseek-pro (CONTRACT file-ownership). ADC oneshot at 12 dB with
 * curve-fitting calibration (ruling R3b); when the calibration scheme
 * cannot be created, WARN once and keep the linear fallback
 * (cb_math_adc_raw_to_mv) — that is not an init failure. The divider (x3)
 * and gain (x700/695) conversion lives in the pure cb_battery_math module
 * (frozen ruling: never inside cb_math). Plausibility logging is
 * transition-gated (ruling R6b) and there is no per-read data line —
 * examples own the data lines. Contract: never aborts (optional part).
 */
#include "cb_battery.h"

#include "cb_battery_math.h"
#include "cb_math.h"
#include "cb_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "cb_battery";

static adc_oneshot_unit_handle_t s_adc;   /* NULL until init succeeded */
static adc_cali_handle_t s_cali;          /* NULL when uncalibrated */
static bool s_calibrated;
static bool s_warned_implausible;         /* R6b transition gate */
static SemaphoreHandle_t s_mutex;         /* NULL until init succeeded */

/* Frozen enum bindings (cb_lcd ruling-1 style: no raw casts). */
_Static_assert(CB_BAT_ADC_UNIT == ADC_UNIT_1, "frozen ADC unit");
_Static_assert(CB_BAT_ADC_CHANNEL == ADC_CHANNEL_0, "frozen ADC channel");

esp_err_t cb_battery_init(void)
{
    if (s_adc != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGW(TAG, "no memory for mutex");
        return ESP_ERR_NO_MEM;
    }

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = CB_BAT_ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "oneshot unit failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        s_adc = NULL;
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_adc, CB_BAT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "channel config failed: %s", esp_err_to_name(err));
        (void)adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }

    /* R3b: curve fitting only; failure degrades to the linear fallback and
     * is NOT an init failure. */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = CB_BAT_ADC_UNIT,
        .chan = CB_BAT_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    s_calibrated = (err == ESP_OK);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "curve fitting unavailable: %s — linear fallback",
                 esp_err_to_name(err));
        s_cali = NULL;
    }
#else
    s_calibrated = false;
#endif

    ESP_LOGI(TAG, "adc up, cali %s", s_calibrated ? "curve" : "fallback");
    return ESP_OK;
}

esp_err_t cb_battery_read(cb_battery_sample_t *out)
{
    cb_battery_sample_t sample = { 0 };
    int cali_mv = 0;
    esp_err_t err;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_adc == NULL || s_mutex == NULL) {
        ESP_LOGW(TAG, "not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "busy");
        return ESP_ERR_TIMEOUT;
    }

    err = adc_oneshot_read(s_adc, CB_BAT_ADC_CHANNEL, &sample.raw);
    if (err == ESP_OK) {
        if (s_cali != NULL) {
            err = adc_cali_raw_to_voltage(s_cali, sample.raw, &cali_mv);
            if (err == ESP_OK) {
                sample.pin_mv = cali_mv;
                sample.calibrated = true;
            }
        }
        if (s_cali == NULL || err != ESP_OK) {
            sample.pin_mv = (int32_t)cb_math_adc_raw_to_mv(sample.raw, 0, false);
            sample.calibrated = false;
            err = ESP_OK;   /* fallback is data, not an error */
        }
        sample.battery_mv = cb_battery_math_mv_from_pin_mv(sample.pin_mv);
        sample.plausible = cb_battery_math_plausible(sample.battery_mv);
    }
    xSemaphoreGive(s_mutex);

    if (err != ESP_OK) {
        return err;   /* out untouched */
    }

    /* R6b: transition-gated WARN / recovery INFO; no per-read data line. */
    if (!sample.plausible) {
        if (!s_warned_implausible) {
            s_warned_implausible = true;
            ESP_LOGW(TAG, "implausible %ld mV (no cell inserted?)",
                     (long)sample.battery_mv);
        }
    } else if (s_warned_implausible) {
        s_warned_implausible = false;
        ESP_LOGI(TAG, "reading plausible again");
    }

    *out = sample;
    return ESP_OK;
}
