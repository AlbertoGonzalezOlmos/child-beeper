/*
 * cb_bsp.c — Child Beeper BSP skeleton (item 1).
 *
 * Owner: mimo26-pro. Contract: never aborts the boot; the R5 error policy is
 * implemented here exactly:
 *   - parts are attempted only when requested (parts & flag), in the frozen
 *     order of k_parts[]: BACKLIGHT, RGB, I2C, IMU, BUTTON, BATTERY, LCD,
 *     LVGL, SD (SD last: mount can block);
 *   - a requested-but-not-yet-implemented part (init == NULL in item 1) logs
 *     "WARN ... not implemented" and, if strict, contributes
 *     ESP_ERR_NOT_SUPPORTED to the return value (first strict error wins);
 *   - implemented-but-failing parts (items 2+): optional -> WARN + suppressed,
 *     strict -> ERROR + first error returned;
 *   - cb_bsp_init(0) == ESP_OK; every requested part is attempted even after
 *     a failure.
 *
 * Items 2..5 replaced the NULL entries in k_parts[] with their init
 * functions (item 5 = SD last); after item 5 no part is "not implemented".
 */
#include "cb_bsp.h"

#include <stdbool.h>
#include <stddef.h>

#include "cb_backlight.h"
#include "cb_battery.h"
#include "cb_button.h"
#include "cb_i2c.h"
#include "cb_imu.h"
#include "cb_lcd.h"
#include "cb_rgb.h"
#include "cb_sd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "cb_bsp";

typedef esp_err_t (*cb_part_init_fn_t)(void);

typedef struct {
    const char *name;       /* log name; also the docs/api.md part name */
    uint32_t flag;          /* cb_part_t bit */
    bool strict;            /* R5: strict = {LCD, LVGL}, everything else optional */
    cb_part_init_fn_t init; /* NULL = not implemented yet (item 1 skeleton) */
} cb_part_desc_t;

/* Frozen init order — never reorder. */
static const cb_part_desc_t k_parts[] = {
    { "BACKLIGHT", CB_PART_BACKLIGHT, false, cb_backlight_init },
    { "RGB",       CB_PART_RGB,       false, cb_rgb_init },
    { "I2C",       CB_PART_I2C,       false, cb_i2c_init },
    { "IMU",       CB_PART_IMU,       false, cb_imu_init },
    { "BUTTON",    CB_PART_BUTTON,    false, cb_button_init },
    { "BATTERY",   CB_PART_BATTERY,   false, cb_battery_init },
    { "LCD",       CB_PART_LCD,       true,  cb_lcd_init },
    { "LVGL",      CB_PART_LVGL,      true,  cb_lcd_init_lvgl },
    { "SD",        CB_PART_SD,        false, cb_sd_init },
};

/* Item-2 idempotency ruling: parts that returned ESP_OK are marked here and
 * skipped (logged "already initialized") on later cb_bsp_init() calls. A part
 * that failed is NOT marked and is retried on the next call. */
static uint32_t s_inited;

/* ST1 (item 6): per-part last init results, recorded by cb_bsp_init(). The
 * attempted bitmask distinguishes "never attempted" from a recorded
 * ESP_ERR_NOT_FOUND (e.g. an absent IMU). Read/written under s_part_lock.
 * Not host-testable (R11b): the recording loop runs the real k_parts[] init
 * functions inside the IDF build — verified by build + the coordinator's
 * dashboard capture instead. */
static esp_err_t s_part_results[sizeof(k_parts) / sizeof(k_parts[0])];
static uint32_t s_part_attempted;
static portMUX_TYPE s_part_lock = portMUX_INITIALIZER_UNLOCKED;

static void record_part_result(size_t i, esp_err_t err)
{
    portENTER_CRITICAL(&s_part_lock);
    s_part_results[i] = err;
    s_part_attempted |= k_parts[i].flag;
    portEXIT_CRITICAL(&s_part_lock);
}

/* The table must cover every CB_PART_* bit exactly, in enum order. */
_Static_assert(CB_PART_ALL == 0x1FF, "frozen part mask");
_Static_assert((CB_PART_BACKLIGHT | CB_PART_RGB | CB_PART_I2C | CB_PART_IMU |
               CB_PART_BUTTON | CB_PART_BATTERY | CB_PART_LCD | CB_PART_LVGL |
               CB_PART_SD) == CB_PART_ALL,
               "k_parts[] must cover exactly CB_PART_ALL");
_Static_assert(CB_PART_BACKLIGHT == 1u << 0 && CB_PART_LCD == 1u << 6 &&
               CB_PART_SD == 1u << 8,
               "frozen part bit values");

#define CB_BSP_STR_(x) #x
#define CB_BSP_STR(x)  CB_BSP_STR_(x)

static const char k_version[] =
    CB_BSP_STR(CB_BSP_VERSION_MAJOR) "."
    CB_BSP_STR(CB_BSP_VERSION_MINOR) "."
    CB_BSP_STR(CB_BSP_VERSION_PATCH);

const char *cb_bsp_version(void)
{
    return k_version;
}

esp_err_t cb_bsp_part_result(cb_part_t part, esp_err_t *out)
{
    size_t i;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Exactly one known CB_PART_* bit: rejects NONE, ALL, multi-bit masks and
     * unknown bits; the loop below rejects any value that is not a k_parts[]
     * flag at all. */
    if ((uint32_t)part == 0 || ((uint32_t)part & ~(uint32_t)CB_PART_ALL) != 0 ||
        ((uint32_t)part & ((uint32_t)part - 1u)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); ++i) {
        if (k_parts[i].flag == (uint32_t)part) {
            bool attempted;
            esp_err_t result;

            portENTER_CRITICAL(&s_part_lock);
            attempted = (s_part_attempted & k_parts[i].flag) != 0;
            result = s_part_results[i];
            portEXIT_CRITICAL(&s_part_lock);

            if (!attempted) {
                return ESP_ERR_NOT_FOUND;   /* *out untouched */
            }
            *out = result;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;   /* unknown part */
}

esp_err_t cb_bsp_init(uint32_t parts)
{
    esp_err_t first_strict_err = ESP_OK;

    if ((parts & ~(uint32_t)CB_PART_ALL) != 0) {
        ESP_LOGW(TAG, "unknown part bits 0x%03lx ignored",
                 (unsigned long)(parts & ~(uint32_t)CB_PART_ALL));
    }

    for (size_t i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); ++i) {
        const cb_part_desc_t *p = &k_parts[i];

        if ((parts & p->flag) == 0) {
            continue;   /* not requested: skip entirely (fixed order kept) */
        }
        if (s_inited & p->flag) {
            ESP_LOGI(TAG, "%s: already initialized", p->name);
            continue;
        }
        esp_err_t err;

        if (p->init == NULL) {
            /* Requested but not implemented yet (item 1 skeleton). */
            ESP_LOGW(TAG, "%s: not implemented", p->name);
            record_part_result(i, ESP_ERR_NOT_SUPPORTED);
            if (p->strict && first_strict_err == ESP_OK) {
                first_strict_err = ESP_ERR_NOT_SUPPORTED;
            }
            continue;
        }

        err = p->init();
        /* ST1: record the ACTUAL result — optional failures suppressed from
         * the return value are still recorded, so the dashboard sees them. */
        record_part_result(i, err);
        if (err == ESP_OK) {
            s_inited |= p->flag;  /* mark only on success (retry on failure) */
            continue;
        }
        if (p->strict) {
            ESP_LOGE(TAG, "%s: init failed: %s", p->name, esp_err_to_name(err));
            if (first_strict_err == ESP_OK) {
                first_strict_err = err;
            }
        } else {
            ESP_LOGW(TAG, "%s: init failed (optional, suppressed): %s",
                     p->name, esp_err_to_name(err));
        }
    }

    return first_strict_err;
}
