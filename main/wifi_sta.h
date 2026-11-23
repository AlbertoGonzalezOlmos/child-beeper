/*
 * wifi_sta.h — optional Wi-Fi station (STA) for the child-beeper root app.
 * Owner: deepseek-v4-pro (item 8: Wi-Fi). Header form matches cb_math.h /
 * cb_backlight.h (include guard, extern "C", doc comments on every public
 * symbol).
 *
 * Credentials come from /spiflash/wifi_settings.txt on the FAT `storage`
 * partition; the image is generated at build time from the repo-root
 * wifi_settings.txt and flashed together with the app (main/CMakeLists.txt +
 * ./flash.sh). Everything here is non-fatal: with no credentials, or on any
 * failure, Wi-Fi just stays off and the beeper app keeps running (contract:
 * optional parts never abort the process).
 */
#ifndef WIFI_STA_H
#define WIFI_STA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the FAT `storage` partition (wear-levelled, read/write) and
 *        load wifi_settings.txt. Idempotent; every failure is logged and
 *        leaves Wi-Fi disabled, never aborts.
 */
void wifi_sta_init(void);

/**
 * @brief If credentials were loaded, bring up the Wi-Fi station and connect.
 *        The work runs in its own task: this returns immediately and the
 *        caller's heartbeat keeps ticking. @p timeout_ms bounds the wait for
 *        an IP address; a late DHCP answer (after the timeout) still flips
 *        the reported state to connected.
 */
void wifi_sta_connect(uint32_t timeout_ms);

/**
 * @brief One-line status for the app heartbeat, e.g.
 *        "wifi=connected 192.168.1.23". Never longer than @p len - 1 chars.
 */
void wifi_sta_describe(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_H */
