#ifndef CB_SD_H
#define CB_SD_H

/*
 * cb_sd.h — TF card over SDMMC (item 5).
 * Owner: deepseek-pro (CONTRACT file-ownership). Frozen surface (ITEM5-BRIEF
 * SD1, ITEM5-RULINGS SD7/SD8): exactly these six symbols — no cb_sd_card(),
 * no cb_sd_base_path(), no info struct, no format/mkfs API.
 *
 * Contract highlights:
 *  - cb_sd_init() mounts FAT at the frozen mount point "/sdcard" over
 *    SDMMC (4-bit first, automatic logged 1-bit fallback). It NEVER formats:
 *    the one mount site passes format_if_mount_failed = false explicitly
 *    (SD3/SD14). A missing card is a gated WARN + error with
 *    cb_sd_mounted() staying false — never an abort (SD4; optional part).
 *  - cb_sd_unmount() is idempotent: ESP_OK when nothing is mounted; state
 *    is cleared only on success (SD8).
 *  - cb_sd_info() reports bytes via esp_vfs_fat_info(); unmounted ->
 *    ESP_ERR_INVALID_STATE with a one-shot WARN; outputs untouched on
 *    every error. cb_sd_mounted()/cb_sd_bus_width() are silent.
 *  - The driver owns init/transition lines only; examples own data lines.
 */
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent; optional part (last in k_parts[]: mount can block). */
esp_err_t cb_sd_init(void);

/* Idempotent: ESP_OK when nothing is mounted; WARN + error on failure
 * (mounted state kept, SD8). Callers must close their files first. */
esp_err_t cb_sd_unmount(void);

bool cb_sd_mounted(void);

/* 4 or 1 when mounted (negotiated width), 0 when unmounted. */
int cb_sd_bus_width(void);

/* The frozen mount point "/sdcard", valid in every state. */
const char *cb_sd_mount_point(void);

/* Total/free bytes of the mounted FAT volume. NULL args ->
 * ESP_ERR_INVALID_ARG; unmounted -> ESP_ERR_INVALID_STATE (one-shot WARN);
 * both outputs untouched on any error. */
esp_err_t cb_sd_info(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif

#endif /* CB_SD_H */
