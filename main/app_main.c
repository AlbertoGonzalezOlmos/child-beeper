/* Local Wi-Fi messenger, clock and visual alarm. The BSP owns all hardware. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "beeper.h"
#include "cb_bsp.h"
#include "cb_button.h"
#include "cb_lcd.h"
#include "cb_rgb.h"
#include "design_ui.h"
#include "expansion.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "wifi_sta.h"

static const char *TAG = "child_beeper";
static const char *s_replies[] = {"I'm OK", "On my way", "Please call me", "I need help"};
static QueueHandle_t s_buttons;
static QueueHandle_t s_external_actions;
static lv_obj_t *s_system;
static bool s_designed;
static lv_obj_t *s_clock, *s_date, *s_alarm, *s_message, *s_reply, *s_hint, *s_network;
static unsigned s_reply_index;
static unsigned s_edit; /* 0=home, 1=hour, 2=minute, 3=enabled */
static beeper_alarm_t s_draft;
static bool s_save_failed;

/* Timer callback only enqueues; networking, NVS and LVGL run on the main task. */
static void button_event(cb_btn_event_t event, void *unused)
{
    (void)unused;
    if (event >= CB_BTN_EV_CLICK && s_buttons) xQueueSend(s_buttons, &event, 0);
}

static void external_button(design_action_t action)
{
    if (s_external_actions) xQueueSend(s_external_actions, &action, 0);
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, "");
    return obj;
}

static void draw_screen(void)
{
    s_system = lv_obj_create(lv_screen_active());
    lv_obj_t *screen = s_system;
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, 172, 320);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(screen, 8, 6, 156, 0x80BFFF);
    lv_label_set_text(title, "CHILD BEEPER");
    s_clock = label(screen, 8, 30, 156, 0xFFFFFF);
    s_date = label(screen, 8, 50, 156, 0xABC0D0);
    s_alarm = label(screen, 8, 70, 156, 0xFFDB61);
    s_message = label(screen, 8, 96, 156, 0xFFFFFF);
    lv_obj_set_height(s_message, 106);
    /* Long messages scroll vertically instead of silently disappearing. */
    lv_label_set_long_mode(s_message, LV_LABEL_LONG_MODE_SCROLL);
    s_reply = label(screen, 8, 210, 156, 0x80BFFF);
    s_hint = label(screen, 8, 231, 156, 0xABC0D0);
    s_network = label(screen, 8, 282, 156, 0x80BFFF);
}

static void handle_button(cb_btn_event_t event, const beeper_view_t *view)
{
    if (view->ringing) {
        beeper_ack();
        s_edit = 0;
        return;
    }
    if (!s_edit) {
        if (event == CB_BTN_EV_LONG_PRESS) {
            s_draft = view->alarm;
            s_edit = 1;
            s_save_failed = false;
        } else if (event == CB_BTN_EV_DOUBLE_CLICK) {
            beeper_send(s_replies[s_reply_index]);
        } else if (view->unread) {
            beeper_ack();
        } else {
            s_reply_index = (s_reply_index + 1) % (sizeof(s_replies) / sizeof(s_replies[0]));
        }
    } else if (event == CB_BTN_EV_LONG_PRESS) {
        s_edit = 0;
    } else if (event == CB_BTN_EV_CLICK) {
        if (s_edit == 1) s_draft.hour = (s_draft.hour + 1) % 24;
        if (s_edit == 2) s_draft.minute = (s_draft.minute + 1) % 60;
        if (s_edit == 3) s_draft.enabled = !s_draft.enabled;
    } else if (event == CB_BTN_EV_DOUBLE_CLICK) {
        if (s_edit < 3) ++s_edit;
        else {
            s_save_failed = beeper_set_alarm(s_draft.enabled, s_draft.hour, s_draft.minute) != ESP_OK;
            if (!s_save_failed) s_edit = 0;
        }
    }
}

static void update_screen(const beeper_view_t *view, uint64_t now_ms,
                          design_scene_t scene, uint64_t elapsed, const design_effect_t *effect)
{
    if (!s_clock || !cb_lcd_lvgl_lock(50)) return;
    char clock[32], date[32], ip[16];
    if (view->clock_valid) {
        time_t local = (time_t)view->local_epoch;
        struct tm tm;
        gmtime_r(&local, &tm);
        strftime(clock, sizeof(clock), "%H:%M:%S", &tm);
        strftime(date, sizeof(date), "%a %d %b", &tm);
    } else {
        strcpy(clock, "--:--:--");
        strcpy(date, "Sync with phone");
    }
    lv_label_set_text(s_clock, clock);
    lv_label_set_text(s_date, date);
    lv_obj_set_style_bg_color(s_system,
        lv_color_hex(view->ringing && (now_ms / 400) % 2 ? 0x704400 : 0x101820), 0);
    if (view->ringing) {
        lv_label_set_text_fmt(s_alarm, "ALARM! %02u:%02u", view->alarm.hour, view->alarm.minute);
    } else {
        lv_label_set_text_fmt(s_alarm, "Alarm %02u:%02u %s", view->alarm.hour, view->alarm.minute,
                             view->alarm.enabled ? "ON" : "OFF");
    }
    if (s_edit) {
        lv_label_set_text_fmt(s_message, "Set daily alarm\n\n%02u:%02u  %s\nEditing: %s",
            s_draft.hour, s_draft.minute, s_draft.enabled ? "ON" : "OFF",
            s_edit == 1 ? "hour" : s_edit == 2 ? "minute" : "enabled");
        lv_label_set_text(s_reply, s_save_failed ? "Save failed. Retry." : "Alarm settings");
        lv_label_set_text(s_hint, "Tap: change\nDouble: next / save\nHold: cancel");
    } else {
        char message[BEEPER_TEXT_MAX + 32];
        snprintf(message, sizeof(message), "%s\n%s",
                 view->latest[0] ? (view->latest_from_device ? "You:" : "Message:") : "Ready to chat",
                 view->latest[0] ? view->latest : "Connect the phone\non the same Wi-Fi.");
        /* Avoid resetting the scrolling animation on every clock tick. */
        if (strcmp(lv_label_get_text(s_message), message)) lv_label_set_text(s_message, message);
        lv_label_set_text_fmt(s_reply, "> %s", s_replies[s_reply_index]);
        lv_label_set_text(s_hint, view->ringing ? "Press BOOT to stop\nVisual alarm: 60s" :
            view->unread ? "Tap: mark read\nDouble: send reply\nHold: set alarm" :
                           "Tap: choose reply\nDouble: send reply\nHold: set alarm");
    }
    bool connected = wifi_sta_address(ip, sizeof(ip));
    lv_label_set_text_fmt(s_network, "%s\nPair: %s", connected ? ip : "Wi-Fi connecting",
                          view->pairing_code);
    if (s_designed) {
        design_ui_visible(!s_edit);
        if (s_edit) {
            lv_obj_remove_flag(s_system, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_system);
        } else {
            lv_obj_add_flag(s_system, LV_OBJ_FLAG_HIDDEN);
            char microphone[32];
            snprintf(microphone, sizeof(microphone), "Mic: %u%%", expansion_microphone_level());
            const char *bindings[9] = {
                "", clock, date, lv_label_get_text(s_alarm), lv_label_get_text(s_message),
                lv_label_get_text(s_reply), lv_label_get_text(s_hint),
                lv_label_get_text(s_network), microphone,
            };
            design_ui_update(scene, elapsed, bindings, effect);
        }
    }
    cb_lcd_lvgl_unlock();
}

void app_main(void)
{
    /* The messenger needs no SD card or sensor probes during startup. */
    esp_err_t board = cb_bsp_init(CB_PART_BACKLIGHT | CB_PART_RGB | CB_PART_BUTTON |
                                  CB_PART_LCD | CB_PART_LVGL);
    if (board != ESP_OK) ESP_LOGW(TAG, "display init: %s", esp_err_to_name(board));
    esp_err_t app = beeper_init();
    if (app != ESP_OK) {
        ESP_LOGE(TAG, "app init: %s", esp_err_to_name(app));
        return;
    }
    s_buttons = xQueueCreate(8, sizeof(cb_btn_event_t));
    s_external_actions = xQueueCreate(8, sizeof(design_action_t));
    cb_button_set_callback(button_event, NULL);
    if (cb_lcd_lvgl_display() && cb_lcd_lvgl_lock(1000)) {
        draw_screen();
        cb_lcd_lvgl_unlock();
    }
    esp_log_level_set("wifi", ESP_LOG_WARN);
    wifi_sta_init();
    /* Wi-Fi initialization mounts the existing FAT partition, including design assets. */
    if (cb_lcd_lvgl_display() && cb_lcd_lvgl_lock(1000)) {
        s_designed = design_ui_init(lv_screen_active());
        cb_lcd_lvgl_unlock();
    }
    esp_err_t expansion = expansion_init(design_ui_hardware(), external_button);
    if (expansion != ESP_OK) ESP_LOGW(TAG, "optional expansion: %s", esp_err_to_name(expansion));
    wifi_sta_connect(20000);
    /* Animation updates at 50 Hz; keep per-pixel INFO logs out of the console. */
    esp_log_level_set("cb_rgb", ESP_LOG_WARN);
    uint64_t next_ui = 0, next_log = 2000, scene_start = 0;
    uint32_t last_notification = 0;
    design_scene_t previous_scene = DESIGN_SCENES;
    uint32_t previous_rgb = UINT32_MAX;
    for (;;) {
        uint64_t now = esp_timer_get_time() / 1000;
        beeper_tick(now);
        beeper_view_t view;
        beeper_view(&view);
        expansion_tick(now);
        cb_btn_event_t event;
        while (s_buttons && xQueueReceive(s_buttons, &event, 0) == pdTRUE) {
            handle_button(event, &view);
            beeper_view(&view);
            next_ui = 0;
        }
        design_action_t action;
        while (s_external_actions && xQueueReceive(s_external_actions, &action, 0) == pdTRUE) {
            if (view.ringing || action == DESIGN_ACK) {
                beeper_ack();
                s_edit = 0;
            } else if (action == DESIGN_EDIT_ALARM) {
                handle_button(CB_BTN_EV_LONG_PRESS, &view);
            } else if (s_edit) {
                handle_button(action == DESIGN_SEND_REPLY ? CB_BTN_EV_DOUBLE_CLICK : CB_BTN_EV_CLICK, &view);
            } else if (action == DESIGN_SEND_REPLY) {
                beeper_send(s_replies[s_reply_index]);
            } else {
                s_reply_index = (s_reply_index + 1) % (sizeof(s_replies) / sizeof(s_replies[0]));
            }
            beeper_view(&view);
            next_ui = 0;
        }
        design_scene_t scene = view.ringing ? DESIGN_ALARM : view.unread ? DESIGN_MESSAGE : DESIGN_HOME;
        if (scene != previous_scene || (scene == DESIGN_MESSAGE && view.notification_id != last_notification)) {
            scene_start = now;
            previous_scene = scene;
        }
        last_notification = view.notification_id;
        design_effect_t effect;
        design_ui_effect(scene, now - scene_start, &effect);
        expansion_effect(&effect);
        uint32_t r = ((effect.color >> 16) & 255) * effect.brightness / 100;
        uint32_t g = ((effect.color >> 8) & 255) * effect.brightness / 100;
        uint32_t b = (effect.color & 255) * effect.brightness / 100;
        uint32_t rgb = (r << 16) | (g << 8) | b;
        if (rgb != previous_rgb) {
            cb_rgb_set(r, g, b);
            previous_rgb = rgb;
        }
        if (now >= next_ui) {
            update_screen(&view, now, scene, now - scene_start, &effect);
            next_ui = now + 20;
        }
        if (now >= next_log) {
            /* USB re-enumerates after boot; print setup info once it is visible. */
            if (next_log == 2000) ESP_LOGI(TAG, "pairing code: %s", view.pairing_code);
            char wifi[48];
            wifi_sta_describe(wifi, sizeof(wifi));
            ESP_LOGI(TAG, "%s clock=%s unread=%d alarm=%s", wifi,
                     view.clock_valid ? "synced" : "waiting", view.unread,
                     view.ringing ? "ringing" : view.alarm.enabled ? "on" : "off");
            next_log = now + 10000;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
