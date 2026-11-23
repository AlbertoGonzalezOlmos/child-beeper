/*
 * cb_imu.c — QMI8658 6-axis IMU over the cb_i2c master bus, item 4.
 * Owner: deepseek-pro (CONTRACT file-ownership).
 *
 * Frozen init sequence (ITEM4-RULINGS R21): probe 0x6A -> 0x6B (log each
 * outcome) -> WHO_AM_I == CB_QMI_WHO_AM_I_VAL (else WARN + error, never an
 * abort) -> CTRL1 = ADDR_AI (R19) -> CTRL2 RMW (+-8 g range, ODR) -> CTRL3
 * RMW (+-512 dps range, ODR) -> CTRL5 RMW (LPF mode 0, bitwise ~MASK clears
 * only — the vendor !MASK bug class is forbidden, R20) -> CTRL7 =
 * ACC_EN | GYR_EN -> `up addr=.. who=.. range=8g/512dps odr=..Hz`.
 *
 * Hardware fix (2026-11-23 capture, HW-2026-11-23.md): after CTRL1 the driver
 * also issues CTRL9 = CB_QMI_CTRL_CMD_AHB_CLOCK_GATING (0x12) — the vendor
 * command our init skipped. Without it the output registers return a
 * free-running counter instead of sensor data (measured: all seven burst
 * words identical and ramping through the whole int16 range). The vendor's
 * completion wait (`while ((STATUSINT & 0x80) == 0)`) is unbounded —
 * docs/troubleshooting.md vendor-bug ledger — so we bound it (100 ms).
 * After the config writes a one-line register readback is logged (permanent
 * boot diagnostics, not debug litter). Round 2 of the hardware hunt
 * (IMU-HW-FAIL2.md) adds a 50 ms post-enable settle and a diagnostic dump
 * (seven single reads + two short bursts + STATUSINT) — permanent lines
 * until the hunt concludes.
 *
 * The 14-byte burst (CB_IMU_RAW_LEN) starts at CB_QMI_TEMP_L and is decoded
 * by the pure cb_imu_math module (R16/R17) — this file re-derives no
 * register layout or sensitivity (R10). All bus reads/writes are serialized
 * by one mutex with a 100 ms bound (R21); busy WARN <= 1 Hz with the first
 * event allowed (R14b). The driver prints init/transition lines only —
 * examples own per-read data lines (R22/R27). Contract: never aborts
 * (optional part, no ESP_ERROR_CHECK).
 */
#include "cb_imu.h"

#include "cb_i2c.h"
#include "cb_pins.h"
#include "cb_qmi8658_regs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "cb_imu";

/* R11: fixed ranges — no runtime switching, no range Kconfig. */
static const unsigned k_accel_range_g = 8;
static const unsigned k_gyro_range_dps = 512;

/* R18: the Kconfig choice CB_IMU_ODR maps to the frozen normal-mode ODR
 * encodings with #if defined(...). Exactly one choice is valid. */
#if defined(CONFIG_CB_IMU_ODR_120)
#define CB_IMU_ODR_ACCEL CB_QMI_ACC_ODR_NORM_120
#define CB_IMU_ODR_GYRO  CB_QMI_GYR_ODR_NORM_120
#define CB_IMU_ODR_HZ    120
#elif defined(CONFIG_CB_IMU_ODR_250)
#define CB_IMU_ODR_ACCEL CB_QMI_ACC_ODR_NORM_250
#define CB_IMU_ODR_GYRO  CB_QMI_GYR_ODR_NORM_250
#define CB_IMU_ODR_HZ    250
#elif defined(CONFIG_CB_IMU_ODR_1000)
#define CB_IMU_ODR_ACCEL CB_QMI_ACC_ODR_NORM_1000
#define CB_IMU_ODR_GYRO  CB_QMI_GYR_ODR_NORM_1000
#define CB_IMU_ODR_HZ    1000
#else
#error "item 4 (R18): exactly one of CB_IMU_ODR_120/250/1000 must be selected"
#endif

/* Bindings against the frozen header (no re-derivation, assertions only). */
_Static_assert(CB_QMI_GZ_H - CB_QMI_TEMP_L + 1 == CB_IMU_RAW_LEN,
               "frozen 14-byte burst");
_Static_assert(CB_QMI_AX_L - CB_QMI_TEMP_L == 2 &&
               CB_QMI_GX_L - CB_QMI_TEMP_L == 8,
               "frozen burst slot offsets");
_Static_assert(CB_QMI_ACC_RANGE_8G == 0x02 && CB_QMI_GYR_RANGE_512DPS == 0x05,
               "ruled range encodings");
_Static_assert(CB_QMI_ACC_ODR_NORM_120 == CB_QMI_GYR_ODR_NORM_120 &&
               CB_QMI_ACC_ODR_NORM_250 == CB_QMI_GYR_ODR_NORM_250 &&
               CB_QMI_ACC_ODR_NORM_1000 == CB_QMI_GYR_ODR_NORM_1000,
               "accel/gyro normal-ODR encodings share one value");
_Static_assert(CB_QMI_CTRL7_ACC_EN == 0x01 && CB_QMI_CTRL7_GYR_EN == 0x02,
               "frozen CTRL7 enable bits");

static i2c_master_dev_handle_t s_dev;   /* NULL until init succeeded */
static SemaphoreHandle_t s_mutex;       /* NULL until init succeeded (R21) */
static uint8_t s_addr;                  /* winning address; 0 = not initialized */
static uint8_t s_who;                   /* cached WHO byte; 0 = never read */

/* R14b: gated-WARN state — check-and-update under s_log_lock, log outside
 * the critical section. */
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;
static TickType_t s_last_busy_log = (TickType_t)-1000;      /* busy WARN, <=1/s */
static TickType_t s_last_decode_fail = (TickType_t)-1000;   /* decode WARN, <=1/s */
static bool s_warned_no_init;    /* pre-init WARN, one-shot */
static bool s_warned_no_bus;     /* no-bus WARN, one-shot */

/* Rate limit (<=1 Hz): decide under the lock, log outside it. The s_last_*
 * timestamps start at (TickType_t)-1000 so the FIRST event always logs
 * (wrap-safe: now - (TickType_t)-1000 == now + 1000, always >= 1000). */
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

/* One-shot gate for a permanent state (cleared again by cb_imu_init). */
static bool one_shot(bool *done)
{
    bool allow;

    portENTER_CRITICAL(&s_log_lock);
    allow = !*done;
    *done = true;
    portEXIT_CRITICAL(&s_log_lock);
    return allow;
}

/* Every transfer uses the bounded CB_QMI_COMM_TIMEOUT_MS (R21). */
static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1,
                                       CB_QMI_COMM_TIMEOUT_MS);
}

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };

    return i2c_master_transmit(s_dev, tx, sizeof(tx), CB_QMI_COMM_TIMEOUT_MS);
}

/* CTRL9 host command (R10's only command macro): write it, then wait for
 * completion exactly the way the vendor does — STATUSINT bit7 set — but
 * BOUNDED (docs/troubleshooting.md vendor-bug ledger: the vendor's while
 * loop is unbounded and hangs on a broken chip). Timeout is a WARN + error,
 * never an abort. */
#define CB_IMU_CTRL9_POLL_STEP_MS 5
#define CB_IMU_CTRL9_POLL_MAX_MS  100

static esp_err_t ctrl9_command(uint8_t cmd)
{
    uint8_t status = 0;
    int elapsed_ms;
    esp_err_t err = write_reg(CB_QMI_CTRL9, cmd);

    if (err != ESP_OK) {
        return err;
    }
    for (elapsed_ms = 0; elapsed_ms < CB_IMU_CTRL9_POLL_MAX_MS;
         elapsed_ms += CB_IMU_CTRL9_POLL_STEP_MS) {
        vTaskDelay(pdMS_TO_TICKS(CB_IMU_CTRL9_POLL_STEP_MS));
        err = read_reg(CB_QMI_STATUSINT, &status);
        if (err != ESP_OK) {
            return err;
        }
        if ((status & 0x80) != 0) {
            ESP_LOGI(TAG, "ctrl9 cmd 0x%02X done in %d ms", (unsigned)cmd,
                     elapsed_ms + CB_IMU_CTRL9_POLL_STEP_MS);
            return ESP_OK;
        }
    }
    ESP_LOGW(TAG, "ctrl9 cmd 0x%02X wait timeout (status 0x%02X)",
             (unsigned)cmd, (unsigned)status);
    return ESP_ERR_TIMEOUT;
}

/* ---- round-2 hardware diagnostics (IMU-HW-FAIL2.md, 2026-11-23) ---- */

/* One bounded 2-byte little-endian read of a register pair (the high byte
 * still depends on auto-increment — part of what the experiment tests).
 * Zeros the buffer on error: a visibly wrong diagnostic value. */
static void read_u16_le(uint8_t reg, uint8_t out[2])
{
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, out, 2,
                                                CB_QMI_COMM_TIMEOUT_MS);
    if (err != ESP_OK) {
        out[0] = 0;
        out[1] = 0;
    }
}

/* One bounded 4-byte burst read (auto-increment across four registers).
 * out[0] is the first byte read. Zeros on error. */
static void read_burst4(uint8_t reg, uint8_t out[4])
{
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, out, 4,
                                                CB_QMI_COMM_TIMEOUT_MS);
    if (err != ESP_OK) {
        out[0] = out[1] = out[2] = out[3] = 0;
    }
}

/* Decisive experiment: seven SEPARATE 2-byte reads (0x33,0x35,0x37,0x39,
 * 0x3B,0x3D,0x3F) vs two short bursts (from 0x33 and 0x35) vs STATUSINT.
 * One line each, permanent boot diagnostics. */
static void imu_diagnostic_dump(void)
{
    uint8_t t[2], ax[2], ay[2], az[2], gx[2], gy[2], gz[2];
    uint8_t b33[4], b35[4];
    uint8_t status = 0;

    read_u16_le(CB_QMI_TEMP_L, t);
    read_u16_le(CB_QMI_AX_L, ax);
    read_u16_le(CB_QMI_AY_L, ay);
    read_u16_le(CB_QMI_AZ_L, az);
    read_u16_le(CB_QMI_GX_L, gx);
    read_u16_le(CB_QMI_GY_L, gy);
    read_u16_le(CB_QMI_GZ_L, gz);
    read_burst4(CB_QMI_TEMP_L, b33);
    read_burst4(CB_QMI_AX_L, b35);
    (void)read_reg(CB_QMI_STATUSINT, &status);

    ESP_LOGI(TAG,
             "single temp=0x%02X%02X ax=0x%02X%02X ay=0x%02X%02X "
             "az=0x%02X%02X gx=0x%02X%02X gy=0x%02X%02X gz=0x%02X%02X",
             (unsigned)t[0], (unsigned)t[1], (unsigned)ax[0], (unsigned)ax[1],
             (unsigned)ay[0], (unsigned)ay[1], (unsigned)az[0], (unsigned)az[1],
             (unsigned)gx[0], (unsigned)gx[1], (unsigned)gy[0], (unsigned)gy[1],
             (unsigned)gz[0], (unsigned)gz[1]);
    ESP_LOGI(TAG,
             "burst33=0x%02X%02X%02X%02X burst35=0x%02X%02X%02X%02X "
             "statusint=0x%02X",
             (unsigned)b33[0], (unsigned)b33[1], (unsigned)b33[2],
             (unsigned)b33[3], (unsigned)b35[0], (unsigned)b35[1],
             (unsigned)b35[2], (unsigned)b35[3], (unsigned)status);
}

esp_err_t cb_imu_init(void)
{
    i2c_master_bus_handle_t bus;
    uint8_t addr;
    uint8_t who;
    uint8_t reg;
    esp_err_t err;

    if (s_dev != NULL) {
        ESP_LOGI(TAG, "already initialized");
        return ESP_OK;
    }

    bus = cb_i2c_bus();
    if (bus == NULL) {
        if (one_shot(&s_warned_no_bus)) {
            ESP_LOGW(TAG, "i2c bus not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGW(TAG, "no memory for mutex");
        return ESP_ERR_NO_MEM;
    }

    /* R21: probe 0x6A then 0x6B, logging the outcome of each attempt. */
    err = i2c_master_probe(bus, CB_QMI_ADDR_0, CB_QMI_COMM_TIMEOUT_MS);
    ESP_LOGI(TAG, "probe 0x6A %s", err == ESP_OK ? "answered" : "no answer");
    if (err == ESP_OK) {
        addr = (uint8_t)CB_QMI_ADDR_0;
    } else {
        err = i2c_master_probe(bus, CB_QMI_ADDR_1, CB_QMI_COMM_TIMEOUT_MS);
        ESP_LOGI(TAG, "probe 0x6B %s", err == ESP_OK ? "answered" : "no answer");
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "no qmi8658 at 0x6A or 0x6B");
            vSemaphoreDelete(s_mutex);
            s_mutex = NULL;
            return ESP_ERR_NOT_FOUND;
        }
        addr = (uint8_t)CB_QMI_ADDR_1;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = CB_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device add failed: %s", esp_err_to_name(err));
        s_dev = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return err;
    }

    /* R21: the init config block and the read path share the one mutex. */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "busy");
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ESP_ERR_TIMEOUT;
    }

    /* WHO_AM_I: mismatch = WARN + error, never an abort (R21). */
    err = read_reg(CB_QMI_WHO_AM_I, &who);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "who_am_i read failed: %s", esp_err_to_name(err));
        goto fail_locked;
    }
    if (who != CB_QMI_WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "who_am_i 0x%02X != 0x%02X", (unsigned)who,
                 (unsigned)CB_QMI_WHO_AM_I_VAL);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail_locked;
    }

    /* CTRL1: address auto-increment, before the first burst (R19). */
    err = write_reg(CB_QMI_CTRL1, CB_QMI_CTRL1_ADDR_AI);
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* CTRL9: AHB clock gating host command, right after CTRL1 (hardware
     * failure fix 2026-11-23; bounded completion wait — see ctrl9_command). */
    err = ctrl9_command(CB_QMI_CTRL_CMD_AHB_CLOCK_GATING);
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* CTRL2 RMW: accel range +-8 g and the ODR choice (R21: frozen
     * masks/offsets only). */
    err = read_reg(CB_QMI_CTRL2, &reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }
    reg &= (uint8_t)~(CB_QMI_ASCALE_MASK | CB_QMI_AODR_MASK);
    reg |= (uint8_t)((CB_QMI_ACC_RANGE_8G << CB_QMI_ASCALE_OFFSET) |
                     CB_IMU_ODR_ACCEL);
    err = write_reg(CB_QMI_CTRL2, reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* CTRL3 RMW: gyro range +-512 dps and the ODR choice. */
    err = read_reg(CB_QMI_CTRL3, &reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }
    reg &= (uint8_t)~(CB_QMI_GSCALE_MASK | CB_QMI_GODR_MASK);
    reg |= (uint8_t)((CB_QMI_GYR_RANGE_512DPS << CB_QMI_GSCALE_OFFSET) |
                     CB_IMU_ODR_GYRO);
    err = write_reg(CB_QMI_CTRL3, reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* CTRL5 RMW: LPF mode 0 (bypass) for accel and gyro (R20). Bitwise
     * complement clears only — the vendor !MASK bug class is forbidden. */
    err = read_reg(CB_QMI_CTRL5, &reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }
    reg &= (uint8_t)~CB_QMI_ALPF_MASK;
    reg &= (uint8_t)~CB_QMI_GLPF_MASK;
    reg |= CB_QMI_LPF_MODE_0;   /* 0x00: bypass (R20) */
    err = write_reg(CB_QMI_CTRL5, reg);
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* CTRL7: enable both sensors (R20 macros). */
    err = write_reg(CB_QMI_CTRL7,
                    (uint8_t)(CB_QMI_CTRL7_ACC_EN | CB_QMI_CTRL7_GYR_EN));
    if (err != ESP_OK) {
        goto fail_cfg;
    }

    /* Round-2 diagnostic (IMU-HW-FAIL2.md): 50 ms settle after CTRL7
     * before the first data read. Keep only if the experiment shows it is
     * required; otherwise remove. */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Permanent boot readback (one line): verify every configured register
     * actually stuck — the evidence line for the hardware failure hunt. */
    {
        uint8_t c1 = 0;
        uint8_t c2 = 0;
        uint8_t c3 = 0;
        uint8_t c7 = 0;
        uint8_t c9 = 0;

        (void)read_reg(CB_QMI_CTRL1, &c1);
        (void)read_reg(CB_QMI_CTRL2, &c2);
        (void)read_reg(CB_QMI_CTRL3, &c3);
        (void)read_reg(CB_QMI_CTRL7, &c7);
        (void)read_reg(CB_QMI_CTRL9, &c9);
        ESP_LOGI(TAG,
                 "readback ctrl1=0x%02X ctrl2=0x%02X ctrl3=0x%02X "
                 "ctrl7=0x%02X ctrl9=0x%02X",
                 (unsigned)c1, (unsigned)c2, (unsigned)c3, (unsigned)c7,
                 (unsigned)c9);
    }

    /* Decisive experiment (IMU-HW-FAIL2.md): single reads vs bursts. */
    imu_diagnostic_dump();

    xSemaphoreGive(s_mutex);

    s_addr = addr;
    s_who = who;
    ESP_LOGI(TAG, "up addr=0x%02X who=0x%02X range=8g/512dps odr=%uHz",
             (unsigned)s_addr, (unsigned)s_who, (unsigned)CB_IMU_ODR_HZ);

    /* The part is up again: re-arm the one-shot WARNs. */
    portENTER_CRITICAL(&s_log_lock);
    s_warned_no_init = false;
    s_warned_no_bus = false;
    portEXIT_CRITICAL(&s_log_lock);
    return ESP_OK;

fail_cfg:
    ESP_LOGW(TAG, "config failed: %s", esp_err_to_name(err));
fail_locked:
    xSemaphoreGive(s_mutex);
    (void)i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return err;   /* failed init marks nothing: retry allowed */
}

esp_err_t cb_imu_read(cb_imu_sample_t *out)
{
    uint8_t raw[CB_IMU_RAW_LEN];
    uint8_t reg = CB_QMI_TEMP_L;
    esp_err_t err;

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_dev == NULL || s_mutex == NULL) {
        if (one_shot(&s_warned_no_init)) {
            ESP_LOGW(TAG, "not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        if (rate_limited(&s_last_busy_log)) {
            ESP_LOGW(TAG, "busy");
        }
        return ESP_ERR_TIMEOUT;
    }

    /* R21/R22: the 14-byte burst; *out is untouched on any bus error. */
    err = i2c_master_transmit_receive(s_dev, &reg, 1, raw, sizeof(raw),
                                      CB_QMI_COMM_TIMEOUT_MS);
    xSemaphoreGive(s_mutex);
    if (err != ESP_OK) {
        return err;   /* examples own the read-failed line (R22/R27) */
    }

    if (!cb_imu_math_decode(raw, k_accel_range_g, k_gyro_range_dps, out)) {
        /* Unreachable with the fixed valid ranges; keep it defensive. */
        if (rate_limited(&s_last_decode_fail)) {
            ESP_LOGW(TAG, "decode failed");
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;   /* R22: ESP_OK even for a noisy sample */
}

int cb_imu_addr(void)
{
    return (int)s_addr;
}

uint8_t cb_imu_who_am_i(void)
{
    return s_who;
}
