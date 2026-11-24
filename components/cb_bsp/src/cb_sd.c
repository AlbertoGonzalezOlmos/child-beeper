/*
 * cb_sd.c — TF card over SDMMC (4-bit, logged 1-bit fallback), item 5.
 * Owner: deepseek-pro (CONTRACT file-ownership).
 *
 * Frozen mechanics (ITEM5-RULINGS SD7/SD8):
 *  - exactly ONE mount site (mount_at_width below) with
 *    format_if_mount_failed = false written explicitly — never auto-format,
 *    ever (SD3/SD14; audit-static bans the = true form);
 *  - 4-bit attempt first; on failure sdmmc_host_deinit() then retry 1-bit
 *    with SDMMC_HOST_FLAG_1BIT (DEINIT_ARG kept); both failures -> gated
 *    WARN + error, no abort (SD4, optional part);
 *  - unmount: esp_vfs_fat_sdcard_unmount() then sdmmc_host_deinit(); state
 *    cleared only on success (SD8);
 *  - getters snapshot state under a portMUX (no tearing between
 *    mounted/width); no mutex is held across mount/unmount calls (SD8);
 *  - cb_sd_info() = esp_vfs_fat_info(), bytes (SD8);
 *  - driver prints init/transition lines only (SD7 byte-exact); examples
 *    own data lines. No ESP_ERROR_CHECK anywhere (optional part).
 *
 * Board facts (hardware.md): no card-detect and no write-protect lines, so
 * a mount error is NOT proof that no card is inserted — the WARN wording
 * stays hedged ("(no card?)"), and docs must record it as "mount failed".
 */
#include "cb_sd.h"

#include "cb_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cb_sd";

/* SD1: frozen mount point. */
static const char k_mount_point[] = "/sdcard";

static sdmmc_card_t *s_card;   /* NULL until mounted */
static uint8_t s_bus_width;    /* 4 or 1 when mounted, 0 unmounted */

/* SD8: state snapshot lock (no tearing between mounted/width) and the
 * house log-gating lock (check-and-update inside, ESP_LOG outside). */
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;
static TickType_t s_last_mount_fail = (TickType_t)-1000;   /* mount-failed WARN, <=1/s */
static bool s_warned_not_mounted;   /* cb_sd_info pre-mount WARN, one-shot */

/* Rate limit (<=1 Hz, first event always logs — wrap-safe: now -
 * (TickType_t)-1000 == now + 1000, always >= 1000). */
static bool rate_limited(TickType_t *last)
{
    TickType_t now = xTaskGetTickCount();
    bool allow;

    portENTER_CRITICAL(&s_log_lock);
    allow = ((TickType_t)(now - *last) >= pdMS_TO_TICKS(1000));
    if (allow) {
        *last = now;
    }
    portEXIT_CRITICAL(&s_log_lock);
    return allow;
}

/* One-shot gate for a permanent state (re-armed by a successful mount). */
static bool one_shot(bool *done)
{
    bool allow;

    portENTER_CRITICAL(&s_log_lock);
    allow = !*done;
    *done = true;
    portEXIT_CRITICAL(&s_log_lock);
    return allow;
}

/* SD3/SD8/SD14: THE one mount site in the whole tree. The no-format field
 * is written out explicitly so a reviewer can see it; audit-static's
 * forbidden-construct rule bans the = true form. */
static esp_err_t mount_at_width(uint8_t width, sdmmc_card_t **out_card)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    host.max_freq_khz = CONFIG_CB_SD_MAX_FREQ_KHZ;   /* SD2: the only knob */
    host.flags = (width == 4 ? SDMMC_HOST_FLAG_4BIT : SDMMC_HOST_FLAG_1BIT) |
                 SDMMC_HOST_FLAG_DEINIT_ARG;

    /* The legacy slot config has no slot_id field (SD8: "from the header
     * default" = whatever SDMMC_SLOT_CONFIG_DEFAULT() provides); the S3
     * default pins are wrong for this board, so override all six. */
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();

    slot.clk = CB_SD_PIN_CLK;
    slot.cmd = CB_SD_PIN_CMD;
    slot.d0 = CB_SD_PIN_D0;
    slot.d1 = CB_SD_PIN_D1;
    slot.d2 = CB_SD_PIN_D2;
    slot.d3 = CB_SD_PIN_D3;
    slot.cd = SDMMC_SLOT_NO_CD;   /* no card-detect line on this board */
    slot.wp = SDMMC_SLOT_NO_WP;   /* no write-protect line either */
    slot.width = width;
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,   /* SD3/SD14: never format, ever */
        .max_files = 5,
        .allocation_unit_size = 16 * 1024, /* format-time-only: inert here */
    };

    return esp_vfs_fat_sdmmc_mount(k_mount_point, &host, &slot, &mount_cfg,
                                   out_card);
}

esp_err_t cb_sd_init(void)
{
    sdmmc_card_t *card = NULL;
    esp_err_t err;

    if (cb_sd_mounted()) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    err = mount_at_width(4, &card);
    if (err != ESP_OK) {
        /* SD8: tear the slot down, then retry 1-bit with the 1BIT flag
         * (DEINIT_ARG kept); the WARN is the frozen SD7 line. */
        ESP_LOGW(TAG, "4-bit mount failed: %s — retrying 1-bit",
                 esp_err_to_name(err));
        (void)sdmmc_host_deinit();
        err = mount_at_width(1, &card);
    }
    if (err != ESP_OK) {
        if (rate_limited(&s_last_mount_fail)) {
            ESP_LOGW(TAG, "mount failed: %s (no card?)", esp_err_to_name(err));
        }
        return err;   /* failed init marks nothing: retry allowed */
    }

    portENTER_CRITICAL(&s_state_lock);
    s_card = card;
    s_bus_width = (uint8_t)(1u << card->log_bus_width);   /* 1 or 4 */
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG, "up mount=%s width=%d max_freq_khz=%d", k_mount_point,
             (int)s_bus_width, (int)CONFIG_CB_SD_MAX_FREQ_KHZ);

    /* The part is up again: re-arm the info one-shot WARN. */
    portENTER_CRITICAL(&s_log_lock);
    s_warned_not_mounted = false;
    portEXIT_CRITICAL(&s_log_lock);
    return ESP_OK;
}

esp_err_t cb_sd_unmount(void)
{
    sdmmc_card_t *card;
    esp_err_t err;

    portENTER_CRITICAL(&s_state_lock);
    card = s_card;
    portEXIT_CRITICAL(&s_state_lock);

    if (card == NULL) {
        return ESP_OK;   /* SD1: idempotent, silent */
    }

    err = esp_vfs_fat_sdcard_unmount(k_mount_point, card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "unmount failed: %s", esp_err_to_name(err));
        return err;   /* SD8: state kept (still mounted) */
    }
    (void)sdmmc_host_deinit();

    portENTER_CRITICAL(&s_state_lock);
    s_card = NULL;
    s_bus_width = 0;
    portEXIT_CRITICAL(&s_state_lock);

    ESP_LOGI(TAG, "unmounted");
    return ESP_OK;
}

bool cb_sd_mounted(void)
{
    bool mounted;

    portENTER_CRITICAL(&s_state_lock);
    mounted = (s_card != NULL);
    portEXIT_CRITICAL(&s_state_lock);
    return mounted;
}

int cb_sd_bus_width(void)
{
    int width;

    portENTER_CRITICAL(&s_state_lock);
    width = (int)s_bus_width;
    portEXIT_CRITICAL(&s_state_lock);
    return width;
}

const char *cb_sd_mount_point(void)
{
    return k_mount_point;
}

esp_err_t cb_sd_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    uint64_t total = 0;
    uint64_t free_b = 0;
    esp_err_t err;

    if (total_bytes == NULL || free_bytes == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cb_sd_mounted()) {
        if (one_shot(&s_warned_not_mounted)) {
            ESP_LOGW(TAG, "not mounted");
        }
        return ESP_ERR_INVALID_STATE;   /* outputs untouched */
    }

    err = esp_vfs_fat_info(k_mount_point, &total, &free_b);
    if (err != ESP_OK) {
        return err;   /* outputs untouched */
    }
    *total_bytes = total;
    *free_bytes = free_b;
    return ESP_OK;
}
