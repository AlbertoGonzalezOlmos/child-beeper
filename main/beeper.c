#include "beeper.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_sta.h"

static const char *TAG = "beeper";
static beeper_model_t s_model;
static SemaphoreHandle_t s_lock;
static nvs_handle_t s_nvs;
static bool s_nvs_open;
static char s_code[7];
static char s_boot[17];
static httpd_handle_t s_server;
static bool s_sntp_started;
static bool s_discovery_started;
static uint64_t s_next_start_ms;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

static esp_err_t save_alarm(const beeper_alarm_t *alarm)
{
    if (!s_nvs_open) return ESP_ERR_INVALID_STATE;
    uint32_t packed = (alarm->enabled ? 0x10000u : 0) | (alarm->hour << 8) | alarm->minute;
    esp_err_t err = nvs_set_u32(s_nvs, "alarm", packed);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t beeper_set_alarm(bool enabled, unsigned hour, unsigned minute)
{
    if (hour > 23 || minute > 59) return ESP_ERR_INVALID_ARG;
    lock();
    beeper_alarm_t alarm = s_model.alarm;
    alarm.enabled = enabled;
    alarm.hour = hour;
    alarm.minute = minute;
    esp_err_t err = save_alarm(&alarm);
    if (err == ESP_OK) {
        s_model.alarm = alarm;
        s_model.ringing_until_ms = 0;
    }
    unlock();
    return err;
}

bool beeper_send(const char *text)
{
    lock();
    bool ok = beeper_model_message(&s_model, text, true, time(NULL));
    if (ok) s_model.unread = false;
    unlock();
    if (ok) ESP_LOGI(TAG, "device message queued");
    return ok;
}

void beeper_ack(void)
{
    lock();
    beeper_model_ack(&s_model);
    unlock();
}

void beeper_view(beeper_view_t *view)
{
    memset(view, 0, sizeof(*view));
    lock();
    if (s_model.count) {
        const beeper_message_t *last = &s_model.messages[s_model.count - 1];
        strcpy(view->latest, last->text);
        view->latest_from_device = last->from_device;
    }
    view->unread = s_model.unread;
    view->notification_id = s_model.notification_id;
    view->ringing = s_model.ringing_until_ms != 0;
    view->alarm = s_model.alarm;
    view->utc_offset_minutes = s_model.utc_offset_minutes;
    strcpy(view->pairing_code, s_code);
    time_t now = time(NULL);
    view->clock_valid = now >= BEEPER_CLOCK_MIN && now < BEEPER_CLOCK_MAX;
    view->local_epoch = now + s_model.utc_offset_minutes * 60;
    unlock();
}

static esp_err_t json_response(httpd_req_t *req, cJSON *json)
{
    char *text = json ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, text);
    free(text);
    return result;
}

static bool authorized(httpd_req_t *req)
{
    char code[16];
    if (httpd_req_get_hdr_value_str(req, "X-Beeper-Key", code, sizeof(code)) != ESP_OK ||
        strcmp(code, s_code)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Enter the pairing code on the beeper screen");
        return false;
    }
    return true;
}

static cJSON *info_json(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "Child Beeper");
    cJSON_AddNumberToObject(root, "protocol", 1);
    cJSON_AddStringToObject(root, "boot_id", s_boot);
    cJSON_AddNumberToObject(root, "port", 80);
    return root;
}

static esp_err_t info_handler(httpd_req_t *req)
{
    return json_response(req, info_json());
}

static esp_err_t state_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *root = info_json();
    lock();
    time_t now = time(NULL);
    cJSON_AddNumberToObject(root, "epoch", now >= BEEPER_CLOCK_MIN ? (double)now : 0);
    cJSON_AddNumberToObject(root, "utc_offset_minutes", s_model.utc_offset_minutes);
    cJSON_AddBoolToObject(root, "unread", s_model.unread);
    cJSON *alarm = cJSON_AddObjectToObject(root, "alarm");
    cJSON_AddBoolToObject(alarm, "enabled", s_model.alarm.enabled);
    cJSON_AddNumberToObject(alarm, "hour", s_model.alarm.hour);
    cJSON_AddNumberToObject(alarm, "minute", s_model.alarm.minute);
    cJSON_AddBoolToObject(alarm, "ringing", s_model.ringing_until_ms != 0);
    cJSON *messages = cJSON_AddArrayToObject(root, "messages");
    for (size_t i = 0; i < s_model.count; ++i) {
        const beeper_message_t *message = &s_model.messages[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", message->id);
        cJSON_AddStringToObject(item, "from", message->from_device ? "device" : "phone");
        cJSON_AddStringToObject(item, "text", message->text);
        cJSON_AddNumberToObject(item, "epoch", (double)message->epoch);
        cJSON_AddItemToArray(messages, item);
    }
    unlock();
    return json_response(req, root);
}

/* Strictly bounded bodies, complete reads, and no silently truncated JSON. */
static cJSON *read_json(httpd_req_t *req)
{
    char body[1025];
    if (!req->content_len || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON body must be 1..1024 bytes");
        return NULL;
    }
    size_t used = 0;
    while (used < req->content_len) {
        int got = httpd_req_recv(req, body + used, req->content_len - used);
        if (got <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Incomplete request");
            return NULL;
        }
        used += got;
    }
    body[used] = 0;
    cJSON *root = NULL;
    if (!memchr(body, 0, used) && !strstr(body, "\\u0000")) {
        root = cJSON_ParseWithOpts(body, NULL, true);
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected a JSON object");
        return NULL;
    }
    return root;
}

static bool integer(const cJSON *item, double min, double max)
{
    return cJSON_IsNumber(item) && isfinite(item->valuedouble) &&
           item->valuedouble >= min && item->valuedouble <= max &&
           floor(item->valuedouble) == item->valuedouble;
}

static esp_err_t write_handler(httpd_req_t *req)
{
    if (!authorized(req)) return ESP_OK;
    cJSON *root = read_json(req);
    if (!root) return ESP_OK;
    bool valid = false;
    esp_err_t saved = ESP_OK;
    if (!strcmp(req->uri, "/api/messages")) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (cJSON_IsString(text)) {
            lock();
            valid = beeper_model_message(&s_model, text->valuestring, false, time(NULL));
            unlock();
            if (valid) ESP_LOGI(TAG, "phone message received; heartbeat active");
        }
    } else if (!strcmp(req->uri, "/api/clock")) {
        cJSON *epoch = cJSON_GetObjectItemCaseSensitive(root, "epoch");
        cJSON *offset = cJSON_GetObjectItemCaseSensitive(root, "utc_offset_minutes");
        valid = integer(epoch, BEEPER_CLOCK_MIN, BEEPER_CLOCK_MAX - 1) &&
                integer(offset, -720, 840);
        if (valid) {
            lock();
            if (s_model.utc_offset_minutes != (int)offset->valuedouble) {
                saved = s_nvs_open ? nvs_set_i32(s_nvs, "offset", (int)offset->valuedouble)
                                   : ESP_ERR_INVALID_STATE;
                if (saved == ESP_OK) saved = nvs_commit(s_nvs);
            }
            if (saved == ESP_OK) {
                s_model.utc_offset_minutes = (int)offset->valuedouble;
                struct timeval tv = {.tv_sec = (time_t)epoch->valuedouble};
                if (settimeofday(&tv, NULL)) saved = ESP_FAIL;
            }
            unlock();
        }
    } else if (!strcmp(req->uri, "/api/alarm")) {
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
        cJSON *hour = cJSON_GetObjectItemCaseSensitive(root, "hour");
        cJSON *minute = cJSON_GetObjectItemCaseSensitive(root, "minute");
        valid = cJSON_IsBool(enabled) && integer(hour, 0, 23) && integer(minute, 0, 59);
        if (valid) saved = beeper_set_alarm(cJSON_IsTrue(enabled), hour->valueint, minute->valueint);
    } else if (!strcmp(req->uri, "/api/ack")) {
        beeper_ack();
        valid = true;
    }
    cJSON_Delete(root);
    if (!valid) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fields or message (1..240 UTF-8 bytes)");
    if (saved != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save settings");
    return state_handler(req);
}

static esp_err_t start_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/api/info", .method = HTTP_GET, .handler = info_handler},
        {.uri = "/api/state", .method = HTTP_GET, .handler = state_handler},
        {.uri = "/api/messages", .method = HTTP_POST, .handler = write_handler},
        {.uri = "/api/clock", .method = HTTP_POST, .handler = write_handler},
        {.uri = "/api/alarm", .method = HTTP_POST, .handler = write_handler},
        {.uri = "/api/ack", .method = HTTP_POST, .handler = write_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &routes[i]);
        if (err != ESP_OK) {
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "messaging HTTP server ready on port 80");
    return ESP_OK;
}

static void discovery_task(void *unused)
{
    (void)unused;
    for (;;) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(42424),
                                      .sin_addr.s_addr = htonl(INADDR_ANY)};
        if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address))) {
            if (fd >= 0) close(fd);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        for (;;) {
            char data[64];
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            int bytes = recvfrom(fd, data, sizeof(data) - 1, 0, (struct sockaddr *)&peer, &len);
            if (bytes < 0) break;
            data[bytes] = 0;
            if (!strcmp(data, "CHILD_BEEPER_DISCOVER_V1")) {
                cJSON *info = info_json();
                char *text = cJSON_PrintUnformatted(info);
                cJSON_Delete(info);
                if (text) {
                    sendto(fd, text, strlen(text), 0, (struct sockaddr *)&peer, len);
                    free(text);
                }
            }
        }
        close(fd);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t beeper_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    beeper_model_init(&s_model);
    /* Do not erase NVS: it contains the persistent alarm and pairing identity. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) err = nvs_open("beeper", NVS_READWRITE, &s_nvs);
    s_nvs_open = err == ESP_OK;
    uint32_t pin = 0, alarm = 0;
    if (s_nvs_open) {
        nvs_get_u32(s_nvs, "pin", &pin);
        if (nvs_get_u32(s_nvs, "alarm", &alarm) == ESP_OK &&
            ((alarm >> 8) & 255) < 24 && (alarm & 255) < 60) {
            s_model.alarm.enabled = (alarm & 0x10000) != 0;
            s_model.alarm.hour = (alarm >> 8) & 255;
            s_model.alarm.minute = alarm & 255;
        }
        int32_t offset = 0;
        if (nvs_get_i32(s_nvs, "offset", &offset) == ESP_OK && offset >= -720 && offset <= 840)
            s_model.utc_offset_minutes = offset;
        nvs_get_i64(s_nvs, "last_alarm", &s_model.alarm.last_fired_minute);
    }
    if (pin < 100000 || pin > 999999) {
        pin = 100000 + esp_random() % 900000;
        if (s_nvs_open) {
            err = nvs_set_u32(s_nvs, "pin", pin);
            if (err == ESP_OK) err = nvs_commit(s_nvs);
        }
    }
    snprintf(s_code, sizeof(s_code), "%06lu", (unsigned long)pin);
    snprintf(s_boot, sizeof(s_boot), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    if (err != ESP_OK) ESP_LOGW(TAG, "settings persistence unavailable: %s", esp_err_to_name(err));
    return ESP_OK;
}

void beeper_tick(uint64_t monotonic_ms)
{
    lock();
    bool fired = beeper_model_tick(&s_model, time(NULL), monotonic_ms);
    if (fired) {
        char text[40];
        snprintf(text, sizeof(text), "Alarm %02u:%02u", s_model.alarm.hour, s_model.alarm.minute);
        beeper_model_message(&s_model, text, true, time(NULL));
    }
    if (fired && s_nvs_open) {
        esp_err_t err = nvs_set_i64(s_nvs, "last_alarm", s_model.alarm.last_fired_minute);
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        if (err != ESP_OK) ESP_LOGW(TAG, "could not persist alarm occurrence");
    }
    unlock();
    if (fired) ESP_LOGI(TAG, "alarm ringing (60 seconds or until dismissed)");

    if (monotonic_ms < s_next_start_ms) return;
    s_next_start_ms = monotonic_ms + 5000;
    char ip[16];
    if (!wifi_sta_address(ip, sizeof(ip))) return;
    if (!s_server && start_server() != ESP_OK) ESP_LOGW(TAG, "HTTP start failed; will retry");
    if (!s_sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        s_sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
    }
    if (!s_discovery_started) {
        s_discovery_started = xTaskCreate(discovery_task, "discovery", 4096, NULL, 2, NULL) == pdPASS;
    }
}
