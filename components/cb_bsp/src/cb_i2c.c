/*
 * cb_i2c.c — shared I2C master bus on CB_I2C_PORT, item 4.
 * Owner: deepseek-pro (CONTRACT file-ownership).
 *
 * CB_PART_I2C owns the bus (frozen k_parts[] order: I2C before IMU). The
 * frozen surface is cb_i2c_init() + cb_i2c_bus() only (ITEM4-RULINGS R14/R15:
 * no probe helper, no recovery/reset API in item 4). Internal pull-ups are
 * enabled and logged; the line `I i2c: internal pull-ups enabled` is frozen
 * byte-exact (R12/R15) — TAG "i2c" is part of that contract. The bus is
 * never deleted by device drivers; the i2c_master driver serializes
 * transactions. Contract: never aborts (optional part, no ESP_ERROR_CHECK).
 */
#include "cb_i2c.h"

#include "cb_pins.h"
#include "esp_log.h"

/* R15: the frozen line is `I i2c: internal pull-ups enabled`. */
static const char *TAG = "i2c";

static i2c_master_bus_handle_t s_bus;   /* NULL until init succeeded */

/* Frozen port binding (ruling-1 style: no raw enum casts). */
_Static_assert(CB_I2C_PORT == I2C_NUM_0, "frozen I2C port");

esp_err_t cb_i2c_init(void)
{
    if (s_bus != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port = CB_I2C_PORT,
        .sda_io_num = CB_PIN_I2C_SDA,
        .scl_io_num = CB_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,   /* R12 */
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bus init failed: %s", esp_err_to_name(err));
        s_bus = NULL;   /* failed init marks nothing: retry allowed */
        return err;
    }

    ESP_LOGI(TAG, "internal pull-ups enabled");   /* R12/R15: frozen line */
    ESP_LOGI(TAG, "bus up port %d, scl %d, sda %d, %d kHz",
             (int)CB_I2C_PORT, (int)CB_PIN_I2C_SCL, (int)CB_PIN_I2C_SDA,
             (int)(CB_I2C_FREQ_HZ / 1000));
    return ESP_OK;
}

i2c_master_bus_handle_t cb_i2c_bus(void)
{
    return s_bus;
}
