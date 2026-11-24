/* examples/06_dashboard/main/dashboard.c — self-test firmware + LVGL dashboard
 * (toolkit item 6).
 *
 * Serial contract (ITEM6-RULINGS gold table). The contract block is printed
 * bare (no log prefix) so the wire lines are byte-identical to the frozen
 * table; live lines start after RESULT; `alive` is an ESP_LOGI line:
 *
 *   SELFTEST BEGIN version=1 cb_bsp=0.5.0
 *   ITEM:lifecycle PASS init=ESP_OK
 *   ITEM:lcd PASS lvgl=9.5.0 frames=1
 *   ITEM:rgb PASS cap_percent=20
 *   ITEM:button SKIP reason=needs-human poller=running
 *   ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1
 *   ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99
 *   ITEM:sd SKIP reason=no-card
 *   RESULT:PASS total=7 pass=5 fail=0 skip=2
 *
 * Evidence rules (ST6 honesty): lifecycle = cb_bsp_init(CB_PART_ALL) return;
 * lcd = LVGL up + the example observed its own first-frame REFR_READY event
 * (frames=1 — not pixel correctness); rgb = driver init + the frozen
 * CB_RGB_DIM_CAP_PERCENT cap; button = the frozen SKIP line, always — never
 * PASS/FAIL and never guessed from a timed window (a human verifies
 * gestures); battery = one fresh calibrated read; imu = addr + WHO + one
 * resting magnitude (0.50-1.50 g window); sd = mounted + bus width (needs a
 * card). FAIL/SKIP never abort: all seven ITEM lines and exactly one RESULT
 * line are always emitted, back-to-back with nothing in between.
 *
 * One task (app_main). Every lv_* call runs under cb_lcd_lvgl_lock() with a
 * finite timeout; a lock timeout skips that UI update and emits a one-shot
 * gated WARN — it can never silence serial output. On-screen battery/IMU
 * values come from the same sample structs the serial lines print.
 * Build-verified; hardware-pending docs/verification.md checklist 10.
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "cb_battery.h"
#include "cb_bsp.h"
#include "cb_imu.h"
#include "cb_lcd.h"
#include "cb_rgb_math.h"
#include "cb_sd.h"
#include "lvgl.h"

static const char *TAG = "dashboard";

#define SELFTEST_VERSION   1               /* frozen grammar version=1 */
#define LOOP_PERIOD_MS     2000            /* live tick */
#define ALIVE_PERIOD_US    5000000LL       /* heartbeat, accumulating deadlines */
#define ONE_TICK_US        (1000000 / CONFIG_FREERTOS_HZ)  /* seed bias (04/05) */
#define FIRST_FRAME_WAIT_US 1000000LL      /* bounded wait for REFR_READY */
#define LV_BUILD_LOCK_MS   1000            /* UI build lock budget */
#define LV_UPDATE_LOCK_MS  100             /* loop update lock budget (01) */
#define IMU_MAG_MIN_G      0.5f            /* resting |a| window (a reading, not accuracy) */
#define IMU_MAG_MAX_G      1.5f
#define ROW0_Y             44              /* first status row */
#define ROW_H              18              /* per-row height (default font) */

/* Frozen selftest token order (also drives the on-screen rows and the emit
 * order — single source). */
static const char *const k_items[7] = {
    "lifecycle", "lcd", "rgb", "button", "battery", "imu", "sd",
};

/* Evidence gathered once after cb_bsp_init(); feeds the selftest block and
 * the dashboard rows. */
typedef struct {
    esp_err_t lifecycle_err;
    esp_err_t lcd_err, lvgl_err;
    bool      frames_flushed;
    esp_err_t rgb_err;
    esp_err_t bat_part_err;
    bool      bat_ok;
    esp_err_t bat_read_err;
    cb_battery_sample_t bat;
    esp_err_t imu_part_err;
    int       imu_addr;
    uint8_t   imu_who;
    bool      imu_ok;
    esp_err_t imu_read_err;
    cb_imu_sample_t imu;
    bool      sd_mounted;
    int       sd_width;
} evidence_t;

/* One composed ITEM line: verdict word + fields (" k=v ..." with leading
 * space; SKIP leads with " reason=..."). */
typedef struct {
    const char *verdict;
    char fields[80];
} item_line_t;

static lv_obj_t *s_rows[7];
static lv_obj_t *s_bat_row, *s_imu_row, *s_alive_row;
static bool s_ui_built;
static volatile bool s_frames_done;
static bool s_lv_warned, s_bat_warned, s_imu_warned;

/* ------------------------------------------------------------------ helpers */

static esp_err_t part_result(cb_part_t part)
{
    esp_err_t out = ESP_OK;
    esp_err_t q = cb_bsp_part_result(part, &out);
    return (q == ESP_OK) ? out : q;
}

static float imu_mag(const cb_imu_sample_t *s)
{
    return sqrtf(s->ax_g * s->ax_g + s->ay_g * s->ay_g + s->az_g * s->az_g);
}

/* Live halves (serial + screen share these; same structs, same strings).
 * Unavailable sources degrade to the frozen NA forms: bat=NAmV,
 * imu_mag=NAg, addr=0x00 — never a stale value presented as current. */
static void fmt_bat_part(char *buf, size_t n,
                         const cb_battery_sample_t *bat, bool ok)
{
    if (ok) {
        snprintf(buf, n, "bat=%" PRId32 "mV plausible=%d",
                 bat->battery_mv, (int)bat->plausible);
    } else {
        snprintf(buf, n, "bat=NAmV plausible=0");
    }
}

static void fmt_imu_part(char *buf, size_t n, const cb_imu_sample_t *imu,
                         bool ok, int addr)
{
    if (ok) {
        snprintf(buf, n, "imu_mag=%.2fg addr=0x%02X",
                 (double)imu_mag(imu), (unsigned)addr);
    } else if (addr != 0) {
        snprintf(buf, n, "imu_mag=NAg addr=0x%02X", (unsigned)addr);
    } else {
        snprintf(buf, n, "imu_mag=NAg addr=0x00");
    }
}

/* Gated WARNs: first event allowed, one-shot per failing run, re-armed on
 * success (R7/R7b carried rules). */
static void lv_lock_warn(void)
{
    if (!s_lv_warned) {
        ESP_LOGW(TAG, "lvgl lock timeout");
        s_lv_warned = true;
    }
}

/* ------------------------------------------------------------- LVGL widgets */

static void on_refr_ready(lv_event_t *ev)
{
    (void)ev;
    s_frames_done = true;
}

/* Labels only, default font, absolute positions on 172x320; no images, no
 * canvas, no PSRAM (the diagnostic pattern is 01_lcd_hello's job). */
static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_pos(title, 4, 4);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text_fmt(title, "Child Beeper\nselftest cb_bsp=%s",
                          cb_bsp_version());

    for (int i = 0; i < 7; i++) {
        s_rows[i] = lv_label_create(scr);
        lv_obj_set_pos(s_rows[i], 4, ROW0_Y + i * ROW_H);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(0xFFFFFF), 0);
        lv_label_set_text(s_rows[i], k_items[i]);
    }

    s_bat_row = lv_label_create(scr);
    lv_obj_set_pos(s_bat_row, 4, 182);
    lv_label_set_text(s_bat_row, "bat=NAmV plausible=0");

    s_imu_row = lv_label_create(scr);
    lv_obj_set_pos(s_imu_row, 4, 200);
    lv_label_set_text(s_imu_row, "imu_mag=NAg addr=0x00");

    s_alive_row = lv_label_create(scr);
    lv_obj_set_pos(s_alive_row, 4, 220);
    lv_label_set_text(s_alive_row, "alive #0");

    s_ui_built = true;
}

static void apply_rows(const item_line_t out[7], const evidence_t *ev)
{
    if (!s_ui_built) {
        return;
    }
    if (!cb_lcd_lvgl_lock(LV_UPDATE_LOCK_MS)) {
        lv_lock_warn();
        return;
    }
    s_lv_warned = false;
    for (int i = 0; i < 7; i++) {
        uint32_t hex = out[i].verdict[0] == 'P' ? 0x00C853 :
                       out[i].verdict[0] == 'F' ? 0xD50000 : 0xFFB300;
        lv_label_set_text_fmt(s_rows[i], "%s %s", k_items[i], out[i].verdict);
        lv_obj_set_style_text_color(s_rows[i], lv_color_hex(hex), 0);
    }
    char part[48];
    fmt_bat_part(part, sizeof part, &ev->bat, ev->bat_ok);
    lv_label_set_text(s_bat_row, part);
    fmt_imu_part(part, sizeof part, &ev->imu, ev->imu_ok, ev->imu_addr);
    lv_label_set_text(s_imu_row, part);
    cb_lcd_lvgl_unlock();
}

/* ----------------------------------------------------------- selftest block */

static void compose_items(const evidence_t *ev, item_line_t out[7])
{
    /* lifecycle: the cb_bsp_init(CB_PART_ALL) return. */
    if (ev->lifecycle_err == ESP_OK) {
        out[0].verdict = "PASS";
        snprintf(out[0].fields, sizeof out[0].fields, " init=ESP_OK");
    } else {
        out[0].verdict = "FAIL";
        snprintf(out[0].fields, sizeof out[0].fields, " init=%s",
                 esp_err_to_name(ev->lifecycle_err));
    }

    /* lcd: LVGL up + this example's own first frame observed (REFR_READY). */
    snprintf(out[1].fields, sizeof out[1].fields, " lvgl=%d.%d.%d frames=%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             ev->frames_flushed ? 1 : 0);
    out[1].verdict =
        (ev->lcd_err == ESP_OK && ev->lvgl_err == ESP_OK && ev->frames_flushed)
            ? "PASS" : "FAIL";

    /* rgb: driver init + the frozen numeric cap. */
    if (ev->rgb_err == ESP_OK) {
        out[2].verdict = "PASS";
        snprintf(out[2].fields, sizeof out[2].fields, " cap_percent=%d",
                 CB_RGB_DIM_CAP_PERCENT);
    } else {
        out[2].verdict = "FAIL";
        snprintf(out[2].fields, sizeof out[2].fields, " init=%s",
                 esp_err_to_name(ev->rgb_err));
    }

    /* button: frozen constant (ST3) — always SKIP, never guessed. */
    out[3].verdict = "SKIP";
    snprintf(out[3].fields, sizeof out[3].fields,
             " reason=needs-human poller=running");

    /* battery: one fresh calibrated read (implausible data is still data). */
    if (ev->bat_part_err != ESP_OK) {
        out[4].verdict = "FAIL";
        snprintf(out[4].fields, sizeof out[4].fields, " init=%s",
                 esp_err_to_name(ev->bat_part_err));
    } else if (!ev->bat_ok) {
        out[4].verdict = "FAIL";
        snprintf(out[4].fields, sizeof out[4].fields, " read=%s",
                 esp_err_to_name(ev->bat_read_err));
    } else {
        out[4].verdict = ev->bat.plausible ? "PASS" : "FAIL";
        snprintf(out[4].fields, sizeof out[4].fields,
                 " battery_mv=%" PRId32 " pin_mv=%" PRId32
                 " calibrated=%d plausible=%d",
                 ev->bat.battery_mv, ev->bat.pin_mv,
                 (int)ev->bat.calibrated, (int)ev->bat.plausible);
    }

    /* imu: address + WHO + one resting magnitude; absence is a SKIP. */
    if (ev->imu_part_err == ESP_ERR_NOT_FOUND) {
        out[5].verdict = "SKIP";
        snprintf(out[5].fields, sizeof out[5].fields, " reason=absent");
    } else if (ev->imu_part_err != ESP_OK) {
        out[5].verdict = "FAIL";
        snprintf(out[5].fields, sizeof out[5].fields, " init=%s",
                 esp_err_to_name(ev->imu_part_err));
    } else if (ev->imu_addr == 0) {
        out[5].verdict = "FAIL";
        snprintf(out[5].fields, sizeof out[5].fields, " init=%s",
                 esp_err_to_name(ESP_ERR_INVALID_STATE));
    } else if (!ev->imu_ok) {
        out[5].verdict = "FAIL";
        snprintf(out[5].fields, sizeof out[5].fields,
                 " addr=0x%02X who=0x%02X read=%s",
                 (unsigned)ev->imu_addr, (unsigned)ev->imu_who,
                 esp_err_to_name(ev->imu_read_err));
    } else {
        float mag = imu_mag(&ev->imu);
        bool ok = (mag >= IMU_MAG_MIN_G && mag <= IMU_MAG_MAX_G);
        out[5].verdict = ok ? "PASS" : "FAIL";
        snprintf(out[5].fields, sizeof out[5].fields,
                 " addr=0x%02X who=0x%02X mag_g=%.2f",
                 (unsigned)ev->imu_addr, (unsigned)ev->imu_who, (double)mag);
    }

    /* sd: mounted + negotiated width; a missing/unusable card is no-card. */
    if (ev->sd_mounted && ev->sd_width > 0) {
        out[6].verdict = "PASS";
        snprintf(out[6].fields, sizeof out[6].fields, " mounted=1 width=%d",
                 ev->sd_width);
    } else if (ev->sd_mounted) {
        out[6].verdict = "FAIL";
        snprintf(out[6].fields, sizeof out[6].fields, " mounted=1 width=%d",
                 ev->sd_width);
    } else {
        out[6].verdict = "SKIP";
        snprintf(out[6].fields, sizeof out[6].fields, " reason=no-card");
    }
}

/* BEGIN + 7 ITEM lines + exactly one RESULT, counts tallied from the emitted
 * verdicts (never re-derived from evidence: the arithmetic cannot drift). */
static void emit_selftest(const item_line_t out[7])
{
    unsigned p = 0, f = 0, s = 0;
    for (int i = 0; i < 7; i++) {
        if (out[i].verdict[0] == 'P') {
            p++;
        } else if (out[i].verdict[0] == 'F') {
            f++;
        } else {
            s++;
        }
    }

    printf("SELFTEST BEGIN version=%d cb_bsp=%s\n",
           SELFTEST_VERSION, cb_bsp_version());
    for (int i = 0; i < 7; i++) {
        printf("ITEM:%s %s%s\n", k_items[i], out[i].verdict, out[i].fields);
    }
    printf("RESULT:%s total=7 pass=%u fail=%u skip=%u\n",
           f == 0 ? "PASS" : "FAIL", p, f, s);
    fflush(stdout);
}

/* ------------------------------------------------------------------ app_main */

static void gather(evidence_t *ev)
{
    memset(ev, 0, sizeof(*ev));

    ev->lifecycle_err = cb_bsp_init(CB_PART_ALL);
    ev->lcd_err      = part_result(CB_PART_LCD);
    ev->lvgl_err     = part_result(CB_PART_LVGL);
    ev->rgb_err      = part_result(CB_PART_RGB);
    ev->bat_part_err = part_result(CB_PART_BATTERY);
    ev->imu_part_err = part_result(CB_PART_IMU);

    if (ev->bat_part_err == ESP_OK) {
        ev->bat_read_err = cb_battery_read(&ev->bat);
        ev->bat_ok = (ev->bat_read_err == ESP_OK);
    }
    ev->imu_addr = cb_imu_addr();
    ev->imu_who  = cb_imu_who_am_i();
    if (ev->imu_part_err == ESP_OK && ev->imu_addr != 0) {
        ev->imu_read_err = cb_imu_read(&ev->imu);
        ev->imu_ok = (ev->imu_read_err == ESP_OK);
    }
    ev->sd_mounted = cb_sd_mounted();
    ev->sd_width   = cb_sd_bus_width();
}

void app_main(void)
{
    evidence_t ev;
    item_line_t out[7];

    gather(&ev);

    /* Build the dashboard and register the one-shot first-frame observer
     * under one lock hold (cb_lcd P0 precedent: no refresh pass can finish
     * in between). The port-owned LVGL task then refreshes the new UI; its
     * REFR_READY event is this example's own frames=1 evidence. */
    lv_display_t *disp = cb_lcd_lvgl_display();
    if (disp != NULL && cb_lcd_lvgl_lock(LV_BUILD_LOCK_MS)) {
        build_ui();
        lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
        cb_lcd_lvgl_unlock();
        int64_t deadline = esp_timer_get_time() + FIRST_FRAME_WAIT_US;
        while (!s_frames_done && esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    } else {
        lv_lock_warn();
    }
    ev.frames_flushed = s_frames_done;

    /* The whole contract block back-to-back — nothing can interleave. */
    compose_items(&ev, out);
    emit_selftest(out);
    apply_rows(out, &ev);

    /* Live loop: one fresh battery + imu sample per tick feeds BOTH the live
     * serial line and the on-screen rows. */
    TickType_t last = xTaskGetTickCount();
    int64_t next_alive_us = esp_timer_get_time() + ALIVE_PERIOD_US - ONE_TICK_US;
    unsigned alive_n = 0;

    for (;;) {
        cb_battery_sample_t bat;
        bool bat_ok = false;
        if (ev.bat_part_err == ESP_OK) {
            esp_err_t err = cb_battery_read(&bat);
            bat_ok = (err == ESP_OK);
            if (bat_ok) {
                s_bat_warned = false;
            } else if (!s_bat_warned) {
                ESP_LOGW(TAG, "battery read failed: %s", esp_err_to_name(err));
                s_bat_warned = true;
            }
        }

        cb_imu_sample_t imu;
        bool imu_ok = false;
        if (ev.imu_part_err == ESP_OK && ev.imu_addr != 0) {
            esp_err_t err = cb_imu_read(&imu);
            imu_ok = (err == ESP_OK);
            if (imu_ok) {
                s_imu_warned = false;
            } else if (!s_imu_warned) {
                ESP_LOGW(TAG, "imu read failed: %s", esp_err_to_name(err));
                s_imu_warned = true;
            }
        }

        char bat_part[40], imu_part[48];
        fmt_bat_part(bat_part, sizeof bat_part, &bat, bat_ok);
        fmt_imu_part(imu_part, sizeof imu_part, &imu, imu_ok, ev.imu_addr);
        printf("live %s %s\n", bat_part, imu_part);

        /* Heartbeat: accumulating deadline (04/05 rule); overruns resync. */
        int64_t now = esp_timer_get_time();
        if (now >= next_alive_us) {
            alive_n++;
            ESP_LOGI(TAG, "alive #%u", alive_n);
            next_alive_us += ALIVE_PERIOD_US;
            if (now >= next_alive_us) {
                next_alive_us = now + ALIVE_PERIOD_US;
            }
        }

        if (s_ui_built) {
            if (cb_lcd_lvgl_lock(LV_UPDATE_LOCK_MS)) {
                s_lv_warned = false;
                lv_label_set_text(s_bat_row, bat_part);
                lv_label_set_text(s_imu_row, imu_part);
                lv_label_set_text_fmt(s_alive_row, "alive #%u", alive_n);
                cb_lcd_lvgl_unlock();
            } else {
                lv_lock_warn();
            }
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
