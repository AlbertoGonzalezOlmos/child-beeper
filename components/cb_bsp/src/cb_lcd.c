/*
 * cb_lcd.c — ST7789 panel + LVGL port bring-up, item 2. Owner: mimo26-pro
 * (CONTRACT file-ownership). The bring-up sequence reproduces the
 * skeleton-verified main.c (SPI2 40 MHz, CS42/DC41, ST7789 reset 39, RGB
 * element order, gap(34,0), invert_color(true)) minus its out-of-policy
 * ESP_ERROR_CHECKs and its duty-1024 backlight line (item-2 ruling 2: the
 * backlight comes on only after LVGL's first frame). Wiring uses frozen
 * cb_pins.h macros only. Contract: never aborts the process.
 */
#include "cb_lcd.h"

#include "cb_backlight.h"
#include "cb_math.h"
#include "cb_pins.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "cb_lcd";

/* Rule 1: prove the frozen cb_pins.h value matches the interface constant at
 * compile time (in the .c that casts it), so no GPIO identity can silently
 * drift. */
_Static_assert(CB_LCD_SPI_HOST == SPI2_HOST, "frozen SPI host must be SPI2");

#define CB_BACKLIGHT_ON_PERCENT 100  /* item-2 ruling 2: turn-on level */

static esp_lcd_panel_handle_t s_panel;
static lv_display_t          *s_disp;
static bool                   s_port_inited;  /* lvgl_port_init done (A8) */
static bool                   s_lvgl_inited;  /* lvgl_port_add_disp done (A8) */
static bool                   s_bl_hook_fired;

esp_err_t cb_lcd_init(void)
{
    if (s_panel != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    spi_bus_config_t bus = {
        .sclk_io_num   = CB_PIN_LCD_SCLK,
        .mosi_io_num   = CB_PIN_LCD_MOSI,
        .miso_io_num   = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* Skeleton-verified budget: 80 scanlines of RGB565 per transfer. */
        .max_transfer_sz = CB_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize((spi_host_device_t)CB_LCD_SPI_HOST, &bus,
                                      SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num     = CB_PIN_LCD_CS,
        .dc_gpio_num     = CB_PIN_LCD_DC,
        .spi_mode        = 0,
        .pclk_hz         = CB_LCD_PIXEL_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)CB_LCD_SPI_HOST,
                                   &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_new_panel_io_spi: %s", esp_err_to_name(err));
        goto err_bus;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CB_PIN_LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_panel_handle_t panel = NULL;
    err = esp_lcd_new_panel_st7789(io, &panel_cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_new_panel_st7789: %s", esp_err_to_name(err));
        goto err_io;
    }

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_set_gap(panel, CB_LCD_X_GAP, CB_LCD_Y_GAP);
    esp_lcd_panel_invert_color(panel, true);
    esp_lcd_panel_disp_on_off(panel, true);

    s_panel = panel;
    ESP_LOGI(TAG, "display: 172x320 ST7789, spi2 @40MHz, gap(34,0)");
    return ESP_OK;

err_io:
    (void)esp_lcd_panel_io_del(io);
err_bus:
    (void)spi_bus_free((spi_host_device_t)CB_LCD_SPI_HOST);
    return err;
}

/* One-shot: switch the backlight on after the first frame is actually drawn
 * (item-2 ruling 2). Runs in the LVGL task; LEDC calls are task-safe. */
static void on_refr_ready(lv_event_t *ev)
{
    (void)ev;
    if (s_bl_hook_fired) {
        return;
    }
    s_bl_hook_fired = true;
    cb_backlight_set(CB_BACKLIGHT_ON_PERCENT);
    ESP_LOGI(TAG, "first frame done: backlight %d%%", CB_BACKLIGHT_ON_PERCENT);
}

esp_err_t cb_lcd_init_lvgl(void)
{
    if (s_lvgl_inited) {
        ESP_LOGI(TAG, "lvgl already initialized");
        return ESP_OK;
    }
    if (s_panel == NULL) {
        ESP_LOGW(TAG, "lvgl init before cb_lcd_init()");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_port_inited) {
        /* Ruling A7: the port defaults are used unmodified (task priority 4,
         * stack 7168, max sleep 500 ms, timer period 5 ms). */
        const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        esp_err_t err = lvgl_port_init(&port_cfg);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "lvgl_port_init: %s", esp_err_to_name(err));
            return err;
        }
        s_port_inited = true;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle    = NULL,
        .panel_handle = s_panel,
        .buffer_size  = CB_LCD_H_RES * 40,
        .double_buffer = true,
        .hres         = CB_LCD_H_RES,
        .vres         = CB_LCD_V_RES,
        .monochrome   = false,
        .rotation = {
            .swap_xy  = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma         = 1,
            .buff_spiram      = 1,
            .swap_bytes       = 1,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp == NULL) {
        /* A8: the port stays inited (no second LVGL task on retry). */
        ESP_LOGW(TAG, "lvgl_port_add_disp: buffers/disp alloc failed");
        return ESP_ERR_NO_MEM;
    }

    s_disp = disp;
    s_lvgl_inited = true;
    (void)lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    ESP_LOGI(TAG, "lvgl %d.%d up, %dx%d RGB565 x2 (PSRAM+DMA)",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, CB_LCD_H_RES, CB_LCD_V_RES);
    return ESP_OK;
}

esp_lcd_panel_handle_t cb_lcd_panel(void)
{
    return s_panel;
}

lv_display_t *cb_lcd_lvgl_display(void)
{
    return s_disp;
}

bool cb_lcd_lvgl_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void cb_lcd_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
