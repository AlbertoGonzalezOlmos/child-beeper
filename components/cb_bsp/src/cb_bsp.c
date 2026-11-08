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
 * Items 2..5 replace the NULL entries in k_parts[] with their init functions
 * and append their sources to CMakeLists.txt (SRCS) via the review round.
 */
#include "cb_bsp.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_log.h"

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
    { "BACKLIGHT", CB_PART_BACKLIGHT, false, NULL },
    { "RGB",       CB_PART_RGB,       false, NULL },
    { "I2C",       CB_PART_I2C,       false, NULL },
    { "IMU",       CB_PART_IMU,       false, NULL },
    { "BUTTON",    CB_PART_BUTTON,    false, NULL },
    { "BATTERY",   CB_PART_BATTERY,   false, NULL },
    { "LCD",       CB_PART_LCD,       true,  NULL },
    { "LVGL",      CB_PART_LVGL,      true,  NULL },
    { "SD",        CB_PART_SD,        false, NULL },
};

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

esp_err_t cb_bsp_init(uint32_t parts)
{
    esp_err_t first_strict_err = ESP_OK;

    if ((parts & ~(uint32_t)CB_PART_ALL) != 0) {
        ESP_LOGW(TAG, "unknown part bits 0x%03lx ignored",
                 (unsigned long)(parts & ~(uint32_t)CB_PART_ALL));
    }

    for (size_t i = 0; i < sizeof(k_parts) / sizeof(k_parts[0]); ++i) {
        const cb_part_desc_t *p = &k_parts[i];
        esp_err_t err;

        if ((parts & p->flag) == 0) {
            continue;   /* not requested: skip entirely (fixed order kept) */
        }

        if (p->init == NULL) {
            /* Requested but not implemented yet (item 1 skeleton). */
            ESP_LOGW(TAG, "%s: not implemented", p->name);
            if (p->strict && first_strict_err == ESP_OK) {
                first_strict_err = ESP_ERR_NOT_SUPPORTED;
            }
            continue;
        }

        err = p->init();
        if (err == ESP_OK) {
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
