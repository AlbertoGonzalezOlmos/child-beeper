/*
 * 05_sdcard — TF card over SDMMC diagnostic example (toolkit item 5).
 *
 * Exercises the optional cb_sd part (SDMMC slot 1, 4-bit with the driver's
 * logged 1-bit fallback, FAT mounted at the frozen "/sdcard" mount point).
 * The example drives cb_sd directly and deliberately (item-5 capture
 * ruling): exactly ONE mount attempt, and it owns the mount error code for
 * the frozen no-card WARN below. The BSP-wide init path (cb_bsp_init,
 * optional-part suppression) is exercised by the root app, not here. Each
 * round writes a 4 KB file with the deterministic SD11 pattern, reads it
 * back, verifies it byte-for-byte and deletes exactly the file it created:
 * 3 rounds 10 s apart, then a clean unmount. The 5 s alive heartbeat keeps
 * running in every degraded mode and never stops.
 *
 * Serial contract (frozen strings, R23/SD10 — also quoted in README.md):
 *   I (…) sdcard: ready width=%d mount=%s total_bytes=%llu free_bytes=%llu
 *   I (…) sdcard: roundtrip ok path=%s bytes=4096 verified=1 cleanup=deleted
 *   W (…) sdcard: no card or mount failed: %s    (once, then heartbeat only)
 *   W (…) sdcard: roundtrip failed: %s           (one-shot, then polling stops)
 *   I (…) sdcard: alive #%u                      (every 5 s)
 *
 * Cause text for "roundtrip failed: %s": strerror(errno) for I/O failures,
 * "short read" / "verify mismatch at offset %u" for compare failures and
 * esp_err_to_name() for cb_sd_info failures.
 *
 * The mount/probe WARNs, the bus-width fallback lines and the unmount lines
 * are owned by cb_sd (R6b); this example prints data lines only and never
 * aborts on missing hardware. It NEVER formats anything (SD11): it only
 * creates files with O_EXCL and deletes only the exact path it created.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cb_sd.h"

static const char *TAG = "sdcard";

#define LOOP_PERIOD_MS   5000          /* loop tick */
#define ALIVE_PERIOD_US  5000000LL     /* heartbeat every 5 s (accumulating) */
#define RT_PERIOD_US    10000000LL     /* one round-trip every 10 s */
#define RT_ROUNDS       3              /* round-trips before the clean unmount */
#define RT_LEN          4096           /* bytes per round-trip (SD11) */
#define RT_NAME_MAX     99             /* cb05_rt000.tmp .. cb05_rt099.tmp */

/* SD10 freezes the literal "bytes=4096" in the ok line: keep in sync. */
_Static_assert(RT_LEN == 4096, "SD11/SD10: frozen ok line says bytes=4096");

static uint8_t s_wbuf[RT_LEN];         /* deterministic write pattern */
static uint8_t s_rbuf[RT_LEN];         /* read-back buffer */
static char    s_rt_path[64];          /* path this round created */

/*
 * One write -> fsync -> read -> verify -> unlink round on a fresh O_EXCL
 * file (SD11). Returns ESP_OK only when the file was written, verified AND
 * deleted again (that is what "cleanup=deleted" promises); otherwise *cause
 * carries the text for the frozen "roundtrip failed: %s" line. Nothing but
 * s_rt_path is ever created, opened for write, or deleted.
 */
static esp_err_t roundtrip_once(const char **cause)
{
    static char s_mismatch[48];        /* single task: no races */
    int fd;

    for (unsigned n = 0; n <= RT_NAME_MAX; ++n) {
        snprintf(s_rt_path, sizeof(s_rt_path), "%s/cb05_rt%03u.tmp",
                 cb_sd_mount_point(), n);
        fd = open(s_rt_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            *cause = strerror(errno);
            return ESP_FAIL;
        }
    }
    if (fd < 0) {
        *cause = strerror(EEXIST);     /* 000..099 all taken: never clobber */
        return ESP_FAIL;
    }

    for (size_t off = 0; off < RT_LEN; ) {
        ssize_t w = write(fd, s_wbuf + off, RT_LEN - off);
        if (w <= 0) {
            int saved = errno;
            close(fd);
            unlink(s_rt_path);         /* best-effort: our own file only */
            *cause = strerror(saved);
            return ESP_FAIL;
        }
        off += (size_t)w;
    }
    if (fsync(fd) != 0) {
        int saved = errno;
        close(fd);
        unlink(s_rt_path);
        *cause = strerror(saved);
        return ESP_FAIL;
    }
    if (close(fd) != 0) {
        int saved = errno;
        unlink(s_rt_path);
        *cause = strerror(saved);
        return ESP_FAIL;
    }

    fd = open(s_rt_path, O_RDONLY);
    if (fd < 0) {
        int saved = errno;
        unlink(s_rt_path);
        *cause = strerror(saved);
        return ESP_FAIL;
    }
    for (size_t off = 0; off < RT_LEN; ) {
        ssize_t r = read(fd, s_rbuf + off, RT_LEN - off);
        if (r <= 0) {
            int saved = errno;
            close(fd);
            unlink(s_rt_path);
            *cause = (r < 0) ? strerror(saved) : "short read";
            return ESP_FAIL;
        }
        off += (size_t)r;
    }
    if (close(fd) != 0) {
        int saved = errno;
        unlink(s_rt_path);
        *cause = strerror(saved);
        return ESP_FAIL;
    }

    for (size_t i = 0; i < RT_LEN; ++i) {
        if (s_rbuf[i] != s_wbuf[i]) {
            snprintf(s_mismatch, sizeof(s_mismatch),
                     "verify mismatch at offset %u", (unsigned)i);
            unlink(s_rt_path);
            *cause = s_mismatch;
            return ESP_FAIL;
        }
    }

    if (unlink(s_rt_path) != 0) {
        *cause = strerror(errno);      /* no cleanup=deleted without unlink */
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_main(void)
{
    for (size_t i = 0; i < RT_LEN; ++i) {
        s_wbuf[i] = (uint8_t)(i * 31 + 7);   /* SD11: deterministic pattern */
    }

    /* RULING after the item-5 hardware capture: a diagnostic example makes
     * exactly ONE mount attempt, so it drives cb_sd directly (no cb_bsp_init
     * call at all) and owns the mount error code for the frozen WARN below.
     * A missing card is normal on this USB-only board — never abort (SD4). */
    esp_err_t err = cb_sd_init();

    bool rt_enabled = false;
    if (cb_sd_mounted()) {
        uint64_t total_bytes = 0, free_bytes = 0;
        err = cb_sd_info(&total_bytes, &free_bytes);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "ready width=%d mount=%s total_bytes=%llu free_bytes=%llu",
                     cb_sd_bus_width(), cb_sd_mount_point(),
                     (unsigned long long)total_bytes, (unsigned long long)free_bytes);
            rt_enabled = true;
        } else {
            /* SD10 froze exactly one failure line: without capacity the
             * round-trip phase has no ready evidence, so it stops here. */
            ESP_LOGW(TAG, "roundtrip failed: %s", esp_err_to_name(err));
            (void)cb_sd_unmount();
        }
    } else {
        ESP_LOGW(TAG, "no card or mount failed: %s", esp_err_to_name(err));
    }

    unsigned alive = 0;
    unsigned rt_done = 0;
    int64_t now_us = esp_timer_get_time();
    int64_t next_alive_us = now_us + ALIVE_PERIOD_US;   /* first alive at +5 s */
    int64_t next_rt_us = now_us;                        /* first round-trip now */

    for (;;) {
        if (rt_enabled && esp_timer_get_time() >= next_rt_us) {
            const char *cause = "unknown";
            if (roundtrip_once(&cause) == ESP_OK) {
                ESP_LOGI(TAG, "roundtrip ok path=%s bytes=4096 verified=1 cleanup=deleted",
                         s_rt_path);
                if (++rt_done == RT_ROUNDS) {
                    rt_enabled = false;
                    (void)cb_sd_unmount();   /* cb_sd logs cb_sd: unmounted|unmount failed */
                }
            } else {
                /* R13b: one-shot frozen line, then never hammer a dead
                 * part — the alive heartbeat carries the contract on. */
                ESP_LOGW(TAG, "roundtrip failed: %s", cause);
                rt_enabled = false;
                (void)cb_sd_unmount();
            }
            next_rt_us += RT_PERIOD_US;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));

        now_us = esp_timer_get_time();
        if (now_us >= next_alive_us) {
            /* Accumulate (not now + period): keeps the long-run 5 s cadence
             * from drifting — the item-4 review fix. SD10: never stops. */
            next_alive_us += ALIVE_PERIOD_US;
            ESP_LOGI(TAG, "alive #%u", ++alive);
        }
    }
}
