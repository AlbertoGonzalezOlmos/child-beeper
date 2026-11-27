/*
 * wifi_sta.c — Wi-Fi station (STA) support for the child-beeper root app,
 * item 8. Owner: deepseek-v4-pro (CONTRACT file-ownership).
 *
 * Settings file: /spiflash/wifi_settings.txt on the FAT `storage` partition
 * (wear-levelled, read/write). The partition image is generated at build time
 * from the repo-root wifi_settings.txt (main/CMakeLists.txt,
 * fatfs_create_spiflash_image + FLASH_IN_PROJECT), so editing the file and
 * re-running ./flash.sh is the whole provisioning flow.
 *
 * Policy: Wi-Fi is OPTIONAL for this device — no credentials, a mount failure
 * or a failed connect never abort the app (contract: never aborts on optional
 * hardware). The connection attempt runs in its own task so the app heartbeat
 * never stalls while waiting for an IP.
 */
#include "wifi_sta.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sta";

#define MOUNT_POINT      "/spiflash"
#define PARTITION_LABEL  "storage"
#define SETTINGS_PATH    MOUNT_POINT "/wifi_settings.txt"
#define WIFI_TASK_STACK  4096

typedef enum {
    WIFI_STA_OFF = 0,
    WIFI_STA_CONNECTING,
    WIFI_STA_CONNECTED,
    WIFI_STA_FAILED,
} wifi_sta_state_t;

#define BIT_GOT_IP BIT0
#define BIT_FAIL   BIT1

static EventGroupHandle_t s_evt;
static wifi_sta_state_t  s_state = WIFI_STA_OFF;
static char s_ssid[33];        /* 802.11 SSID limit is 32 bytes */
static char s_password[64];    /* WPA2-PSK limit is 63 bytes */
static bool s_credentials;
static bool s_stack_up;
static esp_ip4_addr_t s_ip;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_state(wifi_sta_state_t state)
{
    portENTER_CRITICAL(&s_status_lock);
    s_state = state;
    portEXIT_CRITICAL(&s_status_lock);
}

/* ---- settings parsing ----------------------------------------------------- */

/* Strip leading spaces/tabs and trailing whitespace in place. */
static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t') {
        s++;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return s;
}

/* "ssid=My Wifi" -> key "ssid", value "My Wifi". Returns false for blank
 * lines, comments (#) and lines without a non-empty key=value pair. */
static bool parse_line(char *line, const char **key, const char **value)
{
    char *eq;

    line = trim(line);
    if (*line == '\0' || *line == '#') {
        return false;
    }
    eq = strchr(line, '=');
    if (eq == NULL) {
        return false;
    }
    *eq = '\0';
    *key = trim(line);
    *value = trim(eq + 1);
    return (*key)[0] != '\0' && (*value)[0] != '\0';
}

/* Keep credentials out of console output. */
static bool load_settings(void)
{
    FILE *f = fopen(SETTINGS_PATH, "r");
    char line[128];
    bool got_ssid = false;

    if (f == NULL) {
        ESP_LOGW(TAG, "%s not found — Wi-Fi stays off "
                 "(edit wifi_settings.txt and re-run ./flash.sh)", SETTINGS_PATH);
        return false;
    }
    bool valid = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        const char *key;
        const char *value;
        if (!parse_line(line, &key, &value)) {
            continue;
        }
        if (strcmp(key, "ssid") == 0) {
            if (strlen(value) > 32) valid = false;
            snprintf(s_ssid, sizeof(s_ssid), "%s", value);
            got_ssid = true;
        } else if (strcmp(key, "password") == 0) {
            if (strlen(value) > 63) valid = false;
            snprintf(s_password, sizeof(s_password), "%s", value);
        }
    }
    fclose(f);

    if (!got_ssid || s_ssid[0] == '\0' || strcmp(s_ssid, "YOUR_SSID") == 0) {
        ESP_LOGI(TAG, "no SSID configured — Wi-Fi stays off "
                 "(edit wifi_settings.txt and re-run ./flash.sh)");
        return false;
    }
    if (!valid || strlen(s_password) < 8 || strcmp(s_password, "YOUR_PASSWORD") == 0) {
        ESP_LOGW(TAG, "invalid SSID/password length — check wifi_settings.txt");
        return false;
    }
    ESP_LOGI(TAG, "Wi-Fi settings loaded");
    return true;
}

/* ---- storage partition ---------------------------------------------------- */

static wl_handle_t s_wl = WL_INVALID_HANDLE;

/* Mount the FAT `storage` partition read/write (wear-levelled). The image is
 * pre-formatted at build time, so format_if_mount_failed stays false — the
 * static audit forbids auto-format (data-loss hazard). */
static bool mount_settings_partition(void)
{
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files              = 12, /* Settings plus up to eight designer image streams. */
        .allocation_unit_size   = CONFIG_WL_SECTOR_SIZE,
        .use_one_fat            = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, PARTITION_LABEL,
                                                     &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount %s on %s failed: %s — Wi-Fi stays off",
                 PARTITION_LABEL, MOUNT_POINT, esp_err_to_name(err));
        return false;
    }
    return true;
}

/* ---- Wi-Fi event handling ------------------------------------------------- */

/* Runs on the esp_event task: never blocks, only latches bits + state. */
static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disconnected = data;
        ESP_LOGW(TAG, "disconnected (reason %u); will retry", disconnected->reason);
        portENTER_CRITICAL(&s_status_lock);
        s_state = WIFI_STA_CONNECTING;
        s_ip.addr = 0;
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_evt, BIT_GOT_IP);
        xEventGroupSetBits(s_evt, BIT_FAIL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *evt = (const ip_event_got_ip_t *)data;
        portENTER_CRITICAL(&s_status_lock);
        s_ip = evt->ip_info.ip;
        s_state = WIFI_STA_CONNECTED;   /* also flips state after a late DHCP answer */
        portEXIT_CRITICAL(&s_status_lock);
        xEventGroupClearBits(s_evt, BIT_FAIL);
        xEventGroupSetBits(s_evt, BIT_GOT_IP);
    }
}

/* Shortcut: log the failing call and leave Wi-Fi in the FAILED state. Every
 * Wi-Fi failure is non-fatal by contract — the app keeps running. */
#define WIFI_STA_CHECK(call)                                                   \
    do {                                                                       \
        esp_err_t _err = (call);                                               \
        if (_err != ESP_OK) {                                                  \
            ESP_LOGW(TAG, "%s failed: %s", #call, esp_err_to_name(_err));      \
            set_state(WIFI_STA_FAILED);                                        \
            goto fail;                                                         \
        }                                                                      \
    } while (0)

/* Connect worker: runs in its own task so app_main's heartbeat never stalls.
 * timeout_ms arrives through the task argument. */
static void wifi_task(void *arg)
{
    uint32_t timeout_ms = (uint32_t)(uintptr_t)arg;
    esp_err_t err;

    err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs init failed: %s — Wi-Fi disabled", esp_err_to_name(err));
        set_state(WIFI_STA_FAILED);
        goto fail;
    }

    s_evt = xEventGroupCreate();
    if (s_evt == NULL) {
        ESP_LOGE(TAG, "no memory for the event group");
        set_state(WIFI_STA_FAILED);
        goto fail;
    }

    WIFI_STA_CHECK(esp_netif_init());
    WIFI_STA_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) goto fail;
    WIFI_STA_CHECK(esp_netif_set_hostname(netif, "child-beeper"));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_STA_CHECK(esp_wifi_init(&cfg));
    WIFI_STA_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    WIFI_STA_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL, &instance_any_id));
    WIFI_STA_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, &instance_got_ip));

    /* Zero-initialised: strings shorter than the field stay NUL-terminated. */
    wifi_config_t wifi_cfg = { 0 };
    memcpy(wifi_cfg.sta.ssid, s_ssid, strlen(s_ssid));
    memcpy(wifi_cfg.sta.password, s_password, strlen(s_password));
    /* WPA2-PSK minimum security (open/WEP networks are out of scope). */
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    WIFI_STA_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    WIFI_STA_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    WIFI_STA_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting; reconnects automatically after network loss");
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_evt, BIT_GOT_IP | BIT_FAIL,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
        char ip[16];
        if (wifi_sta_address(ip, sizeof(ip))) {
            if (bits & BIT_GOT_IP) ESP_LOGI(TAG, "connected — ip: %s", ip);
            continue;
        }
        ESP_LOGW(TAG, "no connection; retrying in 5 seconds");
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!wifi_sta_address(ip, sizeof(ip))) esp_wifi_connect();
    }
fail:
    vTaskDelete(NULL);
}

/* ---- public API ----------------------------------------------------------- */

void wifi_sta_init(void)
{
    if (!mount_settings_partition()) {
        return;
    }
    s_credentials = load_settings();
}

void wifi_sta_connect(uint32_t timeout_ms)
{
    if (!s_credentials || s_stack_up) {
        return;
    }
    s_stack_up = true;   /* one worker per boot; it reconnects continuously */
    set_state(WIFI_STA_CONNECTING);

    if (xTaskCreate(wifi_task, "wifi_sta", WIFI_TASK_STACK,
                    (void *)(uintptr_t)timeout_ms, tskIDLE_PRIORITY + 2,
                    NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create the Wi-Fi task");
        set_state(WIFI_STA_FAILED);
    }
}

void wifi_sta_describe(char *buf, size_t len)
{
    portENTER_CRITICAL(&s_status_lock);
    wifi_sta_state_t state = s_state;
    esp_ip4_addr_t address = s_ip;
    portEXIT_CRITICAL(&s_status_lock);
    switch (state) {
    case WIFI_STA_CONNECTED: {
        char ip[16];
        esp_ip4addr_ntoa(&address, ip, sizeof(ip));
        snprintf(buf, len, "wifi=connected %s", ip);
        break;
    }
    case WIFI_STA_CONNECTING:
        snprintf(buf, len, "wifi=connecting");
        break;
    case WIFI_STA_FAILED:
        snprintf(buf, len, "wifi=failed");
        break;
    default:
        snprintf(buf, len, "wifi=off");
        break;
    }
}

bool wifi_sta_address(char *buf, size_t len)
{
    portENTER_CRITICAL(&s_status_lock);
    bool connected = s_state == WIFI_STA_CONNECTED;
    esp_ip4_addr_t address = s_ip;
    portEXIT_CRITICAL(&s_status_lock);
    if (len) buf[0] = 0;
    if (connected) esp_ip4addr_ntoa(&address, buf, len);
    return connected;
}
