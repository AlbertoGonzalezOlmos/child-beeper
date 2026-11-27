#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "cb_pins.h"
#include "cb_math.h"
#include "cb_button_fsm.h"
#include "cb_qmi8658_regs.h"
#include "cb_pattern.h"
#include "cb_rgb_math.h"
#include "cb_battery_math.h"
#include "cb_imu_math.h"
#include "beeper_model.h"
#include "design_model.h"

static int tests_run;
static int tests_failed;
static int current_failed;

static void fail_at(int line, const char *fmt, ...)
{
    va_list ap;
    current_failed = 1;
    printf("# line %d: ", line);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fail_at(__LINE__, "check failed: %s", #condition); \
        } \
    } while (0)

#define CHECK_INT(expected, actual) \
    do { \
        long long check_expected = (long long)(expected); \
        long long check_actual = (long long)(actual); \
        if (check_expected != check_actual) { \
            fail_at(__LINE__, "expected %lld, got %lld", check_expected, check_actual); \
        } \
    } while (0)

static void check_close_at(int line, float expected, float actual, float tolerance)
{
    float difference = expected - actual;
    if (difference < 0.0f) {
        difference = -difference;
    }
    if (difference > tolerance) {
        fail_at(line, "expected %.7f, got %.7f (tolerance %.7f)",
                (double)expected, (double)actual, (double)tolerance);
    }
}

#define CHECK_CLOSE(expected, actual) \
    check_close_at(__LINE__, (expected), (actual), 0.0001f)

static void run_test(void (*test_fn)(void), const char *name)
{
    ++tests_run;
    current_failed = 0;
    test_fn();
    if (current_failed) {
        ++tests_failed;
        printf("not ok %d - %s\n", tests_run, name);
    } else {
        printf("ok %d - %s\n", tests_run, name);
    }
}

static void expect_update(cb_btn_fsm_t *fsm, bool pressed, uint32_t now_ms,
                          cb_btn_event_t expected)
{
    cb_btn_event_t actual = cb_btn_fsm_update(fsm, pressed, now_ms);
    CHECK_INT(expected, actual);
}

static void stable_press(cb_btn_fsm_t *fsm, uint32_t stable_ms)
{
    expect_update(fsm, true, stable_ms - CB_BTN_DEBOUNCE_MS, CB_BTN_EV_NONE);
    expect_update(fsm, true, stable_ms, CB_BTN_EV_PRESS);
}

static void stable_release(cb_btn_fsm_t *fsm, uint32_t stable_ms)
{
    expect_update(fsm, false, stable_ms - CB_BTN_DEBOUNCE_MS, CB_BTN_EV_NONE);
    expect_update(fsm, false, stable_ms, CB_BTN_EV_RELEASE);
}

static void test_pins_lcd(void)
{
    CHECK_INT(40, CB_PIN_LCD_SCLK);
    CHECK_INT(45, CB_PIN_LCD_MOSI);
    CHECK_INT(42, CB_PIN_LCD_CS);
    CHECK_INT(41, CB_PIN_LCD_DC);
    CHECK_INT(39, CB_PIN_LCD_RST);
    CHECK_INT(46, CB_PIN_LCD_BL);
    CHECK_INT(172, CB_LCD_H_RES);
    CHECK_INT(320, CB_LCD_V_RES);
    CHECK_INT(34, CB_LCD_X_GAP);
    CHECK_INT(0, CB_LCD_Y_GAP);
    CHECK_INT(1, CB_LCD_SPI_HOST);
    CHECK_INT(40000000, CB_LCD_PIXEL_CLK_HZ);
    CHECK_INT(4000, CB_LCD_BL_PWM_HZ);
    CHECK_INT(38, CB_PIN_RGB);
}

static void test_pins_sensors_battery(void)
{
    CHECK_INT(47, CB_PIN_I2C_SCL);
    CHECK_INT(48, CB_PIN_I2C_SDA);
    CHECK_INT(0, CB_I2C_PORT);
    CHECK_INT(400000, CB_I2C_FREQ_HZ);
    CHECK_INT(0x6A, CB_QMI_ADDR_0);
    CHECK_INT(0x6B, CB_QMI_ADDR_1);
    CHECK_INT(0x05, CB_QMI_WHO_AM_I_VAL);
    CHECK_INT(0, CB_PIN_BOOT_BTN);
    CHECK_INT(1, CB_PIN_BAT_ADC);
    CHECK_INT(0, CB_BAT_ADC_UNIT);
    CHECK_INT(0, CB_BAT_ADC_CHANNEL);
    CHECK_INT(3, CB_BAT_DIVIDER_NUM);
    CHECK_INT(1, CB_BAT_DIVIDER_DEN);
    CHECK_INT(700, CB_BAT_CAL_GAIN_NUM);
    CHECK_INT(695, CB_BAT_CAL_GAIN_DEN);
}

static void test_pins_sd_and_uniqueness(void)
{
    static const int gpio_pins[] = {
        CB_PIN_LCD_SCLK, CB_PIN_LCD_MOSI, CB_PIN_LCD_CS, CB_PIN_LCD_DC,
        CB_PIN_LCD_RST, CB_PIN_LCD_BL, CB_PIN_RGB, CB_PIN_I2C_SCL,
        CB_PIN_I2C_SDA, CB_PIN_BOOT_BTN, CB_PIN_BAT_ADC, CB_SD_PIN_CLK,
        CB_SD_PIN_CMD, CB_SD_PIN_D0, CB_SD_PIN_D1, CB_SD_PIN_D2, CB_SD_PIN_D3
    };
    size_t i;
    size_t j;
    CHECK_INT(14, CB_SD_PIN_CLK);
    CHECK_INT(15, CB_SD_PIN_CMD);
    CHECK_INT(16, CB_SD_PIN_D0);
    CHECK_INT(18, CB_SD_PIN_D1);
    CHECK_INT(17, CB_SD_PIN_D2);
    CHECK_INT(21, CB_SD_PIN_D3);
    for (i = 0; i < sizeof(gpio_pins) / sizeof(gpio_pins[0]); ++i) {
        for (j = i + 1; j < sizeof(gpio_pins) / sizeof(gpio_pins[0]); ++j) {
            CHECK(gpio_pins[i] != gpio_pins[j]);
        }
    }
}

static void test_qmi_registers(void)
{
    CHECK_INT(0x00, CB_QMI_WHO_AM_I);
    CHECK_INT(0x01, CB_QMI_REVISION_ID);
    CHECK_INT(0x02, CB_QMI_CTRL1);
    CHECK_INT(0x03, CB_QMI_CTRL2);
    CHECK_INT(0x04, CB_QMI_CTRL3);
    CHECK_INT(0x05, CB_QMI_CTRL4);
    CHECK_INT(0x06, CB_QMI_CTRL5);
    CHECK_INT(0x07, CB_QMI_CTRL6);
    CHECK_INT(0x08, CB_QMI_CTRL7);
    CHECK_INT(0x09, CB_QMI_CTRL8);
    CHECK_INT(0x0A, CB_QMI_CTRL9);
    CHECK_INT(0x0B, CB_QMI_CAL1_L);
    CHECK_INT(0x0C, CB_QMI_CAL1_H);
    CHECK_INT(0x0D, CB_QMI_CAL2_L);
    CHECK_INT(0x0E, CB_QMI_CAL2_H);
    CHECK_INT(0x0F, CB_QMI_CAL3_L);
    CHECK_INT(0x10, CB_QMI_CAL3_H);
    CHECK_INT(0x11, CB_QMI_CAL4_L);
    CHECK_INT(0x12, CB_QMI_CAL4_H);
    CHECK_INT(0x2D, CB_QMI_STATUSINT);
    CHECK_INT(0x33, CB_QMI_TEMP_L);
    CHECK_INT(0x34, CB_QMI_TEMP_H);
    CHECK_INT(0x35, CB_QMI_AX_L);
    CHECK_INT(0x36, CB_QMI_AX_H);
    CHECK_INT(0x37, CB_QMI_AY_L);
    CHECK_INT(0x38, CB_QMI_AY_H);
    CHECK_INT(0x39, CB_QMI_AZ_L);
    CHECK_INT(0x3A, CB_QMI_AZ_H);
    CHECK_INT(0x3B, CB_QMI_GX_L);
    CHECK_INT(0x3C, CB_QMI_GX_H);
    CHECK_INT(0x3D, CB_QMI_GY_L);
    CHECK_INT(0x3E, CB_QMI_GY_H);
    CHECK_INT(0x3F, CB_QMI_GZ_L);
    CHECK_INT(0x40, CB_QMI_GZ_H);
}

static void test_qmi_fields_and_ranges(void)
{
    static const int acc_ranges[] = {
        CB_QMI_ACC_RANGE_2G, CB_QMI_ACC_RANGE_4G,
        CB_QMI_ACC_RANGE_8G, CB_QMI_ACC_RANGE_16G
    };
    static const int gyr_ranges[] = {
        CB_QMI_GYR_RANGE_16DPS, CB_QMI_GYR_RANGE_32DPS,
        CB_QMI_GYR_RANGE_64DPS, CB_QMI_GYR_RANGE_128DPS,
        CB_QMI_GYR_RANGE_256DPS, CB_QMI_GYR_RANGE_512DPS,
        CB_QMI_GYR_RANGE_1024DPS
    };
    size_t i;
    CHECK_INT(0x0F, CB_QMI_AODR_MASK);
    CHECK_INT(0x0F, CB_QMI_GODR_MASK);
    CHECK_INT(0x70, CB_QMI_ASCALE_MASK);
    CHECK_INT(0x70, CB_QMI_GSCALE_MASK);
    CHECK_INT(0x06, CB_QMI_ALPF_MASK);
    CHECK_INT(0x60, CB_QMI_GLPF_MASK);
    CHECK_INT(4, CB_QMI_ASCALE_OFFSET);
    CHECK_INT(4, CB_QMI_GSCALE_OFFSET);
    CHECK_INT(1, CB_QMI_ALPF_OFFSET);
    CHECK_INT(5, CB_QMI_GLPF_OFFSET);
    for (i = 0; i < sizeof(acc_ranges) / sizeof(acc_ranges[0]); ++i) {
        CHECK_INT((int)i, acc_ranges[i]);
    }
    for (i = 0; i < sizeof(gyr_ranges) / sizeof(gyr_ranges[0]); ++i) {
        CHECK_INT((int)i, gyr_ranges[i]);
    }
    CHECK_INT(50, CB_QMI_COMM_TIMEOUT_MS);
    CHECK_INT(0x12, CB_QMI_CTRL_CMD_AHB_CLOCK_GATING);
}

static void test_qmi_odr_and_lpf(void)
{
    static const int normal_odr[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    CHECK_INT(normal_odr[0], CB_QMI_ACC_ODR_NORM_8000);
    CHECK_INT(normal_odr[1], CB_QMI_ACC_ODR_NORM_4000);
    CHECK_INT(normal_odr[2], CB_QMI_ACC_ODR_NORM_2000);
    CHECK_INT(normal_odr[3], CB_QMI_ACC_ODR_NORM_1000);
    CHECK_INT(normal_odr[4], CB_QMI_ACC_ODR_NORM_500);
    CHECK_INT(normal_odr[5], CB_QMI_ACC_ODR_NORM_250);
    CHECK_INT(normal_odr[6], CB_QMI_ACC_ODR_NORM_120);
    CHECK_INT(normal_odr[7], CB_QMI_ACC_ODR_NORM_60);
    CHECK_INT(normal_odr[8], CB_QMI_ACC_ODR_NORM_30);
    CHECK_INT(0x0C, CB_QMI_ACC_ODR_LP_128);
    CHECK_INT(0x0D, CB_QMI_ACC_ODR_LP_21);
    CHECK_INT(0x0E, CB_QMI_ACC_ODR_LP_11);
    CHECK_INT(0x0F, CB_QMI_ACC_ODR_LP_3);
    CHECK_INT(normal_odr[0], CB_QMI_GYR_ODR_NORM_8000);
    CHECK_INT(normal_odr[1], CB_QMI_GYR_ODR_NORM_4000);
    CHECK_INT(normal_odr[2], CB_QMI_GYR_ODR_NORM_2000);
    CHECK_INT(normal_odr[3], CB_QMI_GYR_ODR_NORM_1000);
    CHECK_INT(normal_odr[4], CB_QMI_GYR_ODR_NORM_500);
    CHECK_INT(normal_odr[5], CB_QMI_GYR_ODR_NORM_250);
    CHECK_INT(normal_odr[6], CB_QMI_GYR_ODR_NORM_120);
    CHECK_INT(normal_odr[7], CB_QMI_GYR_ODR_NORM_60);
    CHECK_INT(normal_odr[8], CB_QMI_GYR_ODR_NORM_30);
    CHECK_INT(0x00, CB_QMI_LPF_MODE_0);
    CHECK_INT(0x02, CB_QMI_LPF_MODE_1);
    CHECK_INT(0x04, CB_QMI_LPF_MODE_2);
    CHECK_INT(0x06, CB_QMI_LPF_MODE_3);
}

static void test_duty_clamp(void)
{
    CHECK_INT(0, cb_math_duty_from_percent(-1, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(0, cb_math_duty_from_percent(0, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(10, cb_math_duty_from_percent(1, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(511, cb_math_duty_from_percent(50, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(1012, cb_math_duty_from_percent(99, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(1023, cb_math_duty_from_percent(100, CB_MATH_BACKLIGHT_MAX));
    CHECK_INT(1023, cb_math_duty_from_percent(101, CB_MATH_BACKLIGHT_MAX));
}

static void test_duty_custom_maxima(void)
{
    CHECK_INT(0, cb_math_duty_from_percent(50, 0));
    CHECK_INT(0, cb_math_duty_from_percent(50, -10));
    CHECK_INT(50, cb_math_duty_from_percent(50, 100));
    CHECK_INT(99, cb_math_duty_from_percent(100, 99));
    CHECK_INT((int)((99LL * INT_MAX) / 100),
              cb_math_duty_from_percent(99, INT_MAX));
}

static void test_little_endian_decode(void)
{
    static const uint8_t zero[] = {0x00, 0x00};
    static const uint8_t one[] = {0x01, 0x00};
    static const uint8_t max_positive[] = {0xFF, 0x7F};
    static const uint8_t min_negative[] = {0x00, 0x80};
    static const uint8_t minus_one[] = {0xFF, 0xFF};
    static const uint8_t positive[] = {0x34, 0x12};
    CHECK_INT(0, cb_math_le_i16(zero));
    CHECK_INT(1, cb_math_le_i16(one));
    CHECK_INT(INT16_MAX, cb_math_le_i16(max_positive));
    CHECK_INT(INT16_MIN, cb_math_le_i16(min_negative));
    CHECK_INT(-1, cb_math_le_i16(minus_one));
    CHECK_INT(0x1234, cb_math_le_i16(positive));
}

static void test_accel_scales(void)
{
    static const unsigned ranges[] = {2, 4, 8, 16};
    size_t i;
    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        float range = (float)ranges[i];
        CHECK_CLOSE(range / 2.0f, cb_math_accel_lsb_to_g(16384, ranges[i]));
        CHECK_CLOSE(-range, cb_math_accel_lsb_to_g(INT16_MIN, ranges[i]));
        CHECK_CLOSE(range * 32767.0f / 32768.0f,
                    cb_math_accel_lsb_to_g(INT16_MAX, ranges[i]));
    }
}

static void test_gyro_scales(void)
{
    static const unsigned ranges[] = {16, 32, 64, 128, 256, 512, 1024};
    size_t i;
    for (i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        float range = (float)ranges[i];
        CHECK_CLOSE(range / 2.0f, cb_math_gyro_lsb_to_dps(16384, ranges[i]));
        CHECK_CLOSE(-range, cb_math_gyro_lsb_to_dps(INT16_MIN, ranges[i]));
        CHECK_CLOSE(range * 32767.0f / 32768.0f,
                    cb_math_gyro_lsb_to_dps(INT16_MAX, ranges[i]));
    }
}

static void test_qmi_temperature(void)
{
    CHECK_CLOSE(0.0f, cb_math_qmi_temp_c(0x0000));
    CHECK_CLOSE(1.0f, cb_math_qmi_temp_c(0x0100));
    CHECK_CLOSE(25.0f, cb_math_qmi_temp_c(0x1900));
    CHECK_CLOSE(0.5f, cb_math_qmi_temp_c(0x0080));
    CHECK_CLOSE(-1.0f, cb_math_qmi_temp_c(0xFF00));
    CHECK_CLOSE(-0.5f, cb_math_qmi_temp_c(0xFF80));
}

static void test_adc_calibrated(void)
{
    CHECK_CLOSE(1234.0f, cb_math_adc_raw_to_mv(0, 1234, true));
    CHECK_CLOSE(2500.0f, cb_math_adc_raw_to_mv(4095, 2500, true));
    CHECK_CLOSE(0.0f, cb_math_adc_raw_to_mv(-100, 0, true));
}

static void test_adc_fallback_ratio(void)
{
    CHECK_CLOSE(0.0f, cb_math_adc_raw_to_mv(0, 999, false));
    CHECK_CLOSE(3300.0f, cb_math_adc_raw_to_mv(4095, 999, false));
    CHECK_CLOSE(2048.0f * 3300.0f / 4095.0f,
                cb_math_adc_raw_to_mv(2048, 999, false));
    CHECK_CLOSE(0.0f, cb_math_adc_raw_to_mv(-1, 999, false));
    CHECK_CLOSE(3300.0f, cb_math_adc_raw_to_mv(4096, 999, false));
}

static void test_fsm_init_and_idle(void)
{
    cb_btn_fsm_t fsm;
    memset(&fsm, 0xFF, sizeof(fsm));
    cb_btn_fsm_init(&fsm);
    expect_update(&fsm, false, 0, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 20, CB_BTN_EV_NONE);
    expect_update(&fsm, false, UINT32_MAX, CB_BTN_EV_NONE);
}

static void test_fsm_debounce_and_bounce(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    expect_update(&fsm, true, 0, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 10, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 15, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 34, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 35, CB_BTN_EV_PRESS);
    expect_update(&fsm, false, 50, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 55, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 65, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 85, CB_BTN_EV_RELEASE);
    expect_update(&fsm, false, 434, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 435, CB_BTN_EV_CLICK);
}

static void test_fsm_single_click_deferred(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 30);
    stable_release(&fsm, 120);
    expect_update(&fsm, false, 121, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 469, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 470, CB_BTN_EV_CLICK);
    expect_update(&fsm, false, 1000, CB_BTN_EV_NONE);
}

static void test_fsm_double_click(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    stable_release(&fsm, 120);
    stable_press(&fsm, 170);
    stable_release(&fsm, 220);
    expect_update(&fsm, false, 221, CB_BTN_EV_DOUBLE_CLICK);
    expect_update(&fsm, false, 1000, CB_BTN_EV_NONE);
}

static void test_fsm_double_gap_boundary(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    stable_release(&fsm, 120);
    stable_press(&fsm, 170);
    stable_release(&fsm, 470);
    expect_update(&fsm, false, 471, CB_BTN_EV_DOUBLE_CLICK);
}

static void test_fsm_gap_expiry_produces_singles(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    stable_release(&fsm, 120);
    stable_press(&fsm, 170);
    expect_update(&fsm, true, 470, CB_BTN_EV_CLICK);
    stable_release(&fsm, 520);
    expect_update(&fsm, false, 869, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 870, CB_BTN_EV_CLICK);
}

static void test_fsm_no_press_duration_dead_zone(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    stable_release(&fsm, 420);
    expect_update(&fsm, false, 769, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 770, CB_BTN_EV_CLICK);
}

static void test_fsm_long_press(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    expect_update(&fsm, true, 819, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 820, CB_BTN_EV_LONG_PRESS);
    expect_update(&fsm, true, 2000, CB_BTN_EV_NONE);
    stable_release(&fsm, 2120);
    expect_update(&fsm, false, 3000, CB_BTN_EV_NONE);
}

static void test_fsm_long_cancels_pending_click(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    stable_release(&fsm, 120);
    stable_press(&fsm, 170);
    expect_update(&fsm, true, 970, CB_BTN_EV_LONG_PRESS);
    stable_release(&fsm, 1200);
    expect_update(&fsm, false, 2000, CB_BTN_EV_NONE);
}

static void test_fsm_no_duplicate_edges(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, 20);
    expect_update(&fsm, true, 21, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 100, CB_BTN_EV_NONE);
    stable_release(&fsm, 200);
    expect_update(&fsm, false, 201, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 300, CB_BTN_EV_NONE);
    expect_update(&fsm, false, 550, CB_BTN_EV_CLICK);
    expect_update(&fsm, false, 551, CB_BTN_EV_NONE);
}

static void test_fsm_time_wraparound(void)
{
    cb_btn_fsm_t fsm;
    cb_btn_fsm_init(&fsm);
    stable_press(&fsm, UINT32_MAX - 10U);
    expect_update(&fsm, true, 788, CB_BTN_EV_NONE);
    expect_update(&fsm, true, 789, CB_BTN_EV_LONG_PRESS);
    stable_release(&fsm, 920);
    expect_update(&fsm, false, 2000, CB_BTN_EV_NONE);
}

#define PATTERN_MAX_PIXELS (CB_LCD_H_RES * CB_LCD_V_RES)
#define PATTERN_GUARD 0xA55A

static uint16_t pattern_storage[PATTERN_MAX_PIXELS + 2];
static uint16_t *pattern_data = pattern_storage + 1;

static void pattern_reset(void)
{
    size_t i;
    for (i = 0; i < PATTERN_MAX_PIXELS + 2; ++i) {
        pattern_storage[i] = PATTERN_GUARD;
    }
}

static void pattern_check_guards(void)
{
    CHECK_INT(PATTERN_GUARD, pattern_storage[0]);
    CHECK_INT(PATTERN_GUARD, pattern_storage[PATTERN_MAX_PIXELS + 1]);
}

static uint16_t expected_grey(int x, int width)
{
    const uint8_t v = (uint8_t)((int64_t)x * 255 / (width - 1));
    return (uint16_t)(((uint16_t)(v >> 3) << 11) |
                      ((uint16_t)(v >> 2) << 5) |
                      (uint16_t)(v >> 3));
}

static void test_pattern_palette(void)
{
    static const uint16_t expected[] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
        0xF81F, 0xF800, 0x001F, 0x0000
    };
    size_t i;
    for (i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        CHECK_INT(expected[i], cb_pattern_bar_color((int)i));
    }
    CHECK_INT(0x0000, cb_pattern_bar_color(-1));
    CHECK_INT(0x0000, cb_pattern_bar_color(8));
    CHECK_INT(0x0000, cb_pattern_bar_color(INT_MAX));
}

static void test_pattern_solid_and_invalid(void)
{
    const int width = 19;
    const int height = 17;
    int y;
    int x;

    pattern_reset();
    cb_pattern_fill_solid(pattern_data, width, height, 0x1234);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            CHECK_INT(0x1234, pattern_data[y * width + x]);
        }
    }
    pattern_check_guards();

    pattern_reset();
    cb_pattern_fill_solid(pattern_data, 16, 17, 0x1234);
    cb_pattern_fill_solid(pattern_data, 17, 16, 0x1234);
    cb_pattern_fill_diagnostic(pattern_data, 16, 16);
    cb_pattern_fill_solid(0, 172, 320, 0x1234);
    cb_pattern_fill_diagnostic(0, 172, 320);
    for (size_t i = 0; i < PATTERN_MAX_PIXELS + 2; ++i) {
        CHECK_INT(PATTERN_GUARD, pattern_storage[i]);
    }
}

static void test_pattern_corner_markers(void)
{
    int dy;
    int dx;

    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, CB_LCD_H_RES, CB_LCD_V_RES);
    for (dy = 0; dy < 2; ++dy) {
        for (dx = 0; dx < 2; ++dx) {
            CHECK_INT(0xF800, pattern_data[dy * CB_LCD_H_RES + dx]);
            CHECK_INT(0xFFE0,
                      pattern_data[dy * CB_LCD_H_RES + (CB_LCD_H_RES - 2 + dx)]);
            CHECK_INT(0x07FF,
                      pattern_data[(CB_LCD_V_RES - 2 + dy) * CB_LCD_H_RES + dx]);
            CHECK_INT(0xF81F,
                      pattern_data[(CB_LCD_V_RES - 2 + dy) * CB_LCD_H_RES +
                                   (CB_LCD_H_RES - 2 + dx)]);
        }
    }

    CHECK_INT(0xFFFF, pattern_data[2]);
    CHECK_INT(0x0000, pattern_data[CB_LCD_H_RES - 3]);
    CHECK_INT(0x0000, pattern_data[300 * CB_LCD_H_RES]);
    CHECK_INT(0xFFFF, pattern_data[300 * CB_LCD_H_RES + CB_LCD_H_RES - 1]);
    CHECK_INT(expected_grey(2, CB_LCD_H_RES),
              pattern_data[319 * CB_LCD_H_RES + 2]);
    CHECK_INT(expected_grey(169, CB_LCD_H_RES),
              pattern_data[319 * CB_LCD_H_RES + 169]);
    pattern_check_guards();
}

static void test_pattern_bar_boundaries(void)
{
    static const int starts[] = {0, 22, 43, 65, 86, 108, 129, 151};
    static const int ends[] = {21, 42, 64, 85, 107, 128, 150, 171};
    static const int odd_widths[] = {17, 19, 23};
    size_t i;

    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, CB_LCD_H_RES, CB_LCD_V_RES);
    for (i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        CHECK_INT(cb_pattern_bar_color((int)i),
                  pattern_data[2 * CB_LCD_H_RES + starts[i]]);
        CHECK_INT(cb_pattern_bar_color((int)i),
                  pattern_data[2 * CB_LCD_H_RES + ends[i]]);
        if (i > 0) {
            CHECK_INT(cb_pattern_bar_color((int)i - 1),
                      pattern_data[2 * CB_LCD_H_RES + starts[i] - 1]);
        }
    }
    for (int x = 0; x < CB_LCD_H_RES; ++x) {
        const int bar = (int)((int64_t)x * 8 / CB_LCD_H_RES);
        CHECK_INT(cb_pattern_bar_color(bar),
                  pattern_data[2 * CB_LCD_H_RES + x]);
    }

    for (i = 0; i < sizeof(odd_widths) / sizeof(odd_widths[0]); ++i) {
        const int width = odd_widths[i];
        cb_pattern_fill_diagnostic(pattern_data, width, 33);
        for (int x = 0; x < width; ++x) {
            const int bar = (int)((int64_t)x * 8 / width);
            CHECK_INT(cb_pattern_bar_color(bar), pattern_data[2 * width + x]);
        }
    }
    pattern_check_guards();
}

static void test_pattern_ramp(void)
{
    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, CB_LCD_H_RES, CB_LCD_V_RES);

    CHECK_INT(cb_pattern_bar_color(0), pattern_data[299 * CB_LCD_H_RES + 2]);
    CHECK_INT(0x0000, pattern_data[300 * CB_LCD_H_RES]);
    CHECK_INT(0xFFFF, pattern_data[300 * CB_LCD_H_RES + CB_LCD_H_RES - 1]);

    for (int y = 300; y < 318; ++y) {
        for (int x = 0; x < CB_LCD_H_RES; ++x) {
            CHECK_INT(expected_grey(x, CB_LCD_H_RES),
                      pattern_data[y * CB_LCD_H_RES + x]);
        }
    }
    for (int y = 318; y < 320; ++y) {
        for (int x = 2; x < CB_LCD_H_RES - 2; ++x) {
            CHECK_INT(expected_grey(x, CB_LCD_H_RES),
                      pattern_data[y * CB_LCD_H_RES + x]);
        }
    }
    pattern_check_guards();
}

static void test_pattern_stride_and_endian(void)
{
    const int width = 19;
    const int height = 17;
    const uint8_t *bytes = (const uint8_t *)pattern_data;

    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, width, height);
    CHECK_INT(0xF800, pattern_data[0]);
    CHECK_INT(0xFFE0, pattern_data[width - 1]);
    CHECK_INT(0x07FF, pattern_data[(height - 1) * width]);
    CHECK_INT(0xF81F, pattern_data[height * width - 1]);
    CHECK_INT(0x00, bytes[0]);
    CHECK_INT(0xF8, bytes[1]);
    CHECK_INT(cb_pattern_bar_color((int)((int64_t)2 * 8 / width)),
              pattern_data[2 * width + 2]);
    pattern_check_guards();
}

static void test_pattern_odd_geometry(void)
{
    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, 17, 17);
    CHECK_INT(cb_pattern_bar_color((int)((int64_t)2 * 8 / 17)),
              pattern_data[15 * 17 + 2]);
    CHECK_INT(expected_grey(2, 17), pattern_data[16 * 17 + 2]);
    CHECK_INT(0xF800, pattern_data[0]);
    CHECK_INT(0xFFE0, pattern_data[16]);
    CHECK_INT(0x07FF, pattern_data[16 * 17]);
    CHECK_INT(0xF81F, pattern_data[16 * 17 + 16]);
    pattern_check_guards();

    pattern_reset();
    cb_pattern_fill_diagnostic(pattern_data, 19, 33);
    CHECK_INT(cb_pattern_bar_color((int)((int64_t)2 * 8 / 19)),
              pattern_data[30 * 19 + 2]);
    CHECK_INT(expected_grey(2, 19), pattern_data[31 * 19 + 2]);
    CHECK_INT(expected_grey(16, 19), pattern_data[32 * 19 + 16]);
    CHECK_INT(0x07FF, pattern_data[32 * 19]);
    CHECK_INT(0xF81F, pattern_data[32 * 19 + 18]);
    pattern_check_guards();
}

typedef struct {
    cb_btn_event_t events[64];
    size_t count;
} cb_trace_t;

static void trace_update(cb_btn_fsm_t *fsm, bool pressed, uint32_t now_ms,
                         cb_trace_t *trace)
{
    cb_btn_event_t event = cb_btn_fsm_update(fsm, pressed, now_ms);
    if (event == CB_BTN_EV_NONE) {
        return;
    }
    if (trace->count >= sizeof(trace->events) / sizeof(trace->events[0])) {
        fail_at(__LINE__, "event trace overflow");
        return;
    }
    trace->events[trace->count++] = event;
}

static void polled_to(cb_btn_fsm_t *fsm, uint32_t *now_ms, uint32_t stop_ms,
                      bool pressed, cb_trace_t *trace)
{
    while (*now_ms <= stop_ms) {
        trace_update(fsm, pressed, *now_ms, trace);
        *now_ms += 5;
    }
}

static void expect_trace(const cb_trace_t *trace, const cb_btn_event_t *expected,
                         size_t expected_count)
{
    size_t i;
    CHECK_INT(expected_count, trace->count);
    for (i = 0; i < expected_count && i < trace->count; ++i) {
        CHECK_INT(expected[i], trace->events[i]);
    }
}

static void test_fsm_fully_polled_traces(void)
{
    static const cb_btn_event_t single[] = {
        CB_BTN_EV_PRESS, CB_BTN_EV_RELEASE, CB_BTN_EV_CLICK
    };
    static const cb_btn_event_t double_click[] = {
        CB_BTN_EV_PRESS, CB_BTN_EV_RELEASE, CB_BTN_EV_PRESS,
        CB_BTN_EV_RELEASE, CB_BTN_EV_DOUBLE_CLICK
    };
    static const cb_btn_event_t long_press[] = {
        CB_BTN_EV_PRESS, CB_BTN_EV_LONG_PRESS, CB_BTN_EV_RELEASE
    };
    static const cb_btn_event_t long_after_pending[] = {
        CB_BTN_EV_PRESS, CB_BTN_EV_RELEASE, CB_BTN_EV_PRESS,
        CB_BTN_EV_CLICK, CB_BTN_EV_LONG_PRESS, CB_BTN_EV_RELEASE
    };
    static const cb_btn_event_t slow_second[] = {
        CB_BTN_EV_PRESS, CB_BTN_EV_RELEASE, CB_BTN_EV_PRESS,
        CB_BTN_EV_CLICK, CB_BTN_EV_RELEASE, CB_BTN_EV_CLICK
    };
    cb_btn_fsm_t fsm;
    cb_trace_t trace;
    uint32_t now;

    /* Every scenario injects raw input every 5 ms, including while a second
     * press is held. This covers decisions hidden by large test-time jumps. */
    memset(&trace, 0, sizeof(trace));
    cb_btn_fsm_init(&fsm);
    now = 0;
    polled_to(&fsm, &now, 95, true, &trace);
    polled_to(&fsm, &now, 500, false, &trace);
    expect_trace(&trace, single, sizeof(single) / sizeof(single[0]));

    memset(&trace, 0, sizeof(trace));
    cb_btn_fsm_init(&fsm);
    now = 0;
    polled_to(&fsm, &now, 95, true, &trace);
    polled_to(&fsm, &now, 145, false, &trace);
    polled_to(&fsm, &now, 195, true, &trace);
    polled_to(&fsm, &now, 300, false, &trace);
    expect_trace(&trace, double_click, sizeof(double_click) / sizeof(double_click[0]));

    memset(&trace, 0, sizeof(trace));
    cb_btn_fsm_init(&fsm);
    now = 0;
    polled_to(&fsm, &now, 995, true, &trace);
    polled_to(&fsm, &now, 1200, false, &trace);
    expect_trace(&trace, long_press, sizeof(long_press) / sizeof(long_press[0]));

    /* The first deferred CLICK matures at 470 ms during the second hold;
     * LONG_PRESS at 970 ms suppresses any later click from that hold. */
    memset(&trace, 0, sizeof(trace));
    cb_btn_fsm_init(&fsm);
    now = 0;
    polled_to(&fsm, &now, 95, true, &trace);
    polled_to(&fsm, &now, 145, false, &trace);
    polled_to(&fsm, &now, 995, true, &trace);
    polled_to(&fsm, &now, 1300, false, &trace);
    expect_trace(&trace, long_after_pending,
                 sizeof(long_after_pending) / sizeof(long_after_pending[0]));

    /* Second press starts at 300 ms (inside the gap), releases at 600 ms
     * (outside it): two independent CLICKs, the first during the hold. */
    memset(&trace, 0, sizeof(trace));
    cb_btn_fsm_init(&fsm);
    now = 0;
    polled_to(&fsm, &now, 95, true, &trace);
    polled_to(&fsm, &now, 295, false, &trace);
    polled_to(&fsm, &now, 595, true, &trace);
    polled_to(&fsm, &now, 1100, false, &trace);
    expect_trace(&trace, slow_second, sizeof(slow_second) / sizeof(slow_second[0]));
}

static void test_rgb_dim_cap_macro(void)
{
    CHECK_INT(20, CB_RGB_DIM_CAP_PERCENT);
    CHECK_INT(51, 255 * CB_RGB_DIM_CAP_PERCENT / 100);
}

/* R1b: peak-scale to the cap — unchanged at/below the cap, floor scaling
 * above it, idempotent, NULL-safe. */
static void test_rgb_math_dim(void)
{
    uint8_t r, g, b;
    int i;

    r = 255; g = 255; b = 255;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(51, r); CHECK_INT(51, g); CHECK_INT(51, b);

    r = 255; g = 128; b = 0;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(51, r); CHECK_INT(25, g); CHECK_INT(0, b);

    r = 52; g = 0; b = 0;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(51, r); CHECK_INT(0, g); CHECK_INT(0, b);

    /* At/below the cap: unchanged. */
    r = 20; g = 10; b = 5;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(20, r); CHECK_INT(10, g); CHECK_INT(5, b);
    r = 1; g = 1; b = 1;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(1, r); CHECK_INT(1, g); CHECK_INT(1, b);

    /* Idempotence. */
    r = 255; g = 254; b = 0;
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(51, r); CHECK_INT(50, g); CHECK_INT(0, b);
    cb_rgb_math_dim(&r, &g, &b);
    CHECK_INT(51, r); CHECK_INT(50, g); CHECK_INT(0, b);

    /* Every output stays at/below the cap. */
    for (i = 0; i <= 255; ++i) {
        r = (uint8_t)i;
        g = (uint8_t)((i * 3) % 256);
        b = (uint8_t)((i * 7) % 256);
        cb_rgb_math_dim(&r, &g, &b);
        CHECK(r <= 51); CHECK(g <= 51); CHECK(b <= 51);
    }

    /* NULL pointers: no crash, no write. */
    r = 200; g = 100; b = 50;
    cb_rgb_math_dim(NULL, &g, &b);
    CHECK_INT(200, r); CHECK_INT(100, g); CHECK_INT(50, b);
    cb_rgb_math_dim(&r, NULL, &b);
    CHECK_INT(200, r); CHECK_INT(50, b);
    cb_rgb_math_dim(&r, &g, NULL);
    CHECK_INT(200, r); CHECK_INT(100, g);
    cb_rgb_math_dim(NULL, NULL, NULL);
}

static void test_battery_macro_contract(void)
{
    CHECK_INT(3, CB_BAT_DIVIDER_NUM);
    CHECK_INT(1, CB_BAT_DIVIDER_DEN);
    CHECK_INT(700, CB_BAT_CAL_GAIN_NUM);
    CHECK_INT(695, CB_BAT_CAL_GAIN_DEN);
    CHECK_INT(2500, CB_BAT_PLAUSIBLE_MIN_MV);
    CHECK_INT(4500, CB_BAT_PLAUSIBLE_MAX_MV);
}

/* R6: int64 intermediates, negative -> 0, truncating, INT32_MAX saturation. */
static void test_battery_math_conversion(void)
{
    CHECK_INT(0, cb_battery_math_mv_from_pin_mv(0));
    CHECK_INT(0, cb_battery_math_mv_from_pin_mv(-1));
    CHECK_INT(0, cb_battery_math_mv_from_pin_mv(INT32_MIN));
    CHECK_INT(2100, cb_battery_math_mv_from_pin_mv(695));
    CHECK_INT(3021, cb_battery_math_mv_from_pin_mv(1000));
    CHECK_INT(6345, cb_battery_math_mv_from_pin_mv(2100));
    CHECK_INT(9971, cb_battery_math_mv_from_pin_mv(3300));
    CHECK_INT(4027, cb_battery_math_mv_from_pin_mv(1333));
    CHECK_INT(12373, cb_battery_math_mv_from_pin_mv(4095));
    CHECK_INT(INT32_MAX, cb_battery_math_mv_from_pin_mv(INT32_MAX));
}

/* R5: inclusive window [2500, 4500]. */
static void test_battery_math_plausible(void)
{
    CHECK(!cb_battery_math_plausible(2499));
    CHECK(cb_battery_math_plausible(2500));
    CHECK(cb_battery_math_plausible(3500));
    CHECK(cb_battery_math_plausible(4500));
    CHECK(!cb_battery_math_plausible(4501));
    CHECK(!cb_battery_math_plausible(0));
    CHECK(!cb_battery_math_plausible(-1));
}

/* ---- item 4: cb_imu_math decode (R24 names/vectors, R17 contract) ---- */

static void imu_zero_buf(uint8_t raw[CB_IMU_RAW_LEN])
{
    memset(raw, 0, CB_IMU_RAW_LEN);
}

static void imu_put_le16(uint8_t raw[CB_IMU_RAW_LEN], size_t offset, int16_t value)
{
    raw[offset] = (uint8_t)((uint16_t)value & 0xFFu);
    raw[offset + 1] = (uint8_t)(((uint16_t)value >> 8) & 0xFFu);
}

static void test_imu_math_raw_len(void)
{
    CHECK_INT(14, CB_IMU_RAW_LEN);
    CHECK_INT(14, CB_QMI_GZ_H - CB_QMI_TEMP_L + 1);
    CHECK_INT(2, CB_QMI_AX_L - CB_QMI_TEMP_L);
    CHECK_INT(8, CB_QMI_GX_L - CB_QMI_TEMP_L);
}

static void test_imu_math_decode_resting(void)
{
    static const uint8_t raw[CB_IMU_RAW_LEN] = {
        0x00, 0x19, 0, 0, 0, 0, 0x00, 0x10, 0, 0, 0, 0, 0, 0
    };
    cb_imu_sample_t out;

    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(0.0f, out.ax_g);
    CHECK_CLOSE(0.0f, out.ay_g);
    CHECK_CLOSE(1.0f, out.az_g);
    CHECK_CLOSE(0.0f, out.gx_dps);
    CHECK_CLOSE(0.0f, out.gy_dps);
    CHECK_CLOSE(0.0f, out.gz_dps);
    CHECK_CLOSE(25.0f, out.temp_c);
}

static void test_imu_math_decode_accel_sign(void)
{
    static const uint8_t raw[CB_IMU_RAW_LEN] = {
        0, 0, 0x00, 0xE0, 0x00, 0x08, 0x00, 0xF0, 0, 0, 0, 0, 0, 0
    };
    cb_imu_sample_t out;

    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(-2.0f, out.ax_g);
    CHECK_CLOSE(0.5f, out.ay_g);
    CHECK_CLOSE(-1.0f, out.az_g);
    CHECK_CLOSE(0.0f, out.gx_dps);
    CHECK_CLOSE(0.0f, out.gy_dps);
    CHECK_CLOSE(0.0f, out.gz_dps);
    CHECK_CLOSE(0.0f, out.temp_c);
}

static void test_imu_math_decode_gyro(void)
{
    static const uint8_t raw[CB_IMU_RAW_LEN] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x20, 0x00, 0xC0, 0x00, 0x01
    };
    cb_imu_sample_t out;

    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(128.0f, out.gx_dps);
    CHECK_CLOSE(-256.0f, out.gy_dps);
    CHECK_CLOSE(4.0f, out.gz_dps);
}

static void test_imu_math_decode_ranges(void)
{
    static const unsigned accel_ranges[] = { 2, 4, 8, 16 };
    uint8_t raw[CB_IMU_RAW_LEN];
    cb_imu_sample_t out;
    size_t i;

    for (i = 0; i < sizeof(accel_ranges) / sizeof(accel_ranges[0]); ++i) {
        imu_zero_buf(raw);
        imu_put_le16(raw, 2, 4096);
        CHECK(cb_imu_math_decode(raw, accel_ranges[i], 512, &out));
        CHECK_CLOSE((float)accel_ranges[i] / 8.0f, out.ax_g);
    }

    imu_zero_buf(raw);
    imu_put_le16(raw, 8, INT16_MIN);
    CHECK(cb_imu_math_decode(raw, 8, 16, &out));
    CHECK_CLOSE(-16.0f, out.gx_dps);

    imu_zero_buf(raw);
    imu_put_le16(raw, 8, INT16_MAX);
    CHECK(cb_imu_math_decode(raw, 8, 1024, &out));
    CHECK_CLOSE(1024.0f * 32767.0f / 32768.0f, out.gx_dps);
}

static void test_imu_math_decode_temp(void)
{
    static const struct {
        uint16_t raw;
        float expected;
    } cases[] = {
        { 0x0000, 0.0f },
        { 0x0100, 1.0f },
        { 0x1900, 25.0f },
        { 0x0080, 0.5f },
        { 0xFF00, -1.0f },
        { 0x8000, -128.0f },
    };
    uint8_t raw[CB_IMU_RAW_LEN];
    cb_imu_sample_t out;
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        imu_zero_buf(raw);
        imu_put_le16(raw, 0, (int16_t)cases[i].raw);
        CHECK(cb_imu_math_decode(raw, 8, 512, &out));
        CHECK_CLOSE(cases[i].expected, out.temp_c);
    }
}

static void test_imu_math_decode_layout(void)
{
    static const uint8_t raw[CB_IMU_RAW_LEN] = {
        0x00, 0x01, 0x00, 0x10, 0x00, 0x20, 0x00, 0x30,
        0x00, 0x40, 0x00, 0x50, 0x00, 0x60
    };
    cb_imu_sample_t out;

    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(1.0f, out.temp_c);
    CHECK_CLOSE(1.0f, out.ax_g);
    CHECK_CLOSE(2.0f, out.ay_g);
    CHECK_CLOSE(3.0f, out.az_g);
    CHECK_CLOSE(256.0f, out.gx_dps);
    CHECK_CLOSE(320.0f, out.gy_dps);
    CHECK_CLOSE(384.0f, out.gz_dps);
}

static void test_imu_math_decode_invalid(void)
{
    uint8_t raw[CB_IMU_RAW_LEN];
    cb_imu_sample_t sentinel;
    cb_imu_sample_t out;

    imu_zero_buf(raw);
    sentinel.ax_g = 1.0f;
    sentinel.ay_g = 2.0f;
    sentinel.az_g = 3.0f;
    sentinel.gx_dps = 4.0f;
    sentinel.gy_dps = 5.0f;
    sentinel.gz_dps = 6.0f;
    sentinel.temp_c = 7.0f;

    out = sentinel;
    CHECK(!cb_imu_math_decode(NULL, 8, 512, &out));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);

    out = sentinel;
    CHECK(!cb_imu_math_decode(raw, 8, 512, NULL));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);

    out = sentinel;
    CHECK(!cb_imu_math_decode(raw, 3, 512, &out));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);

    out = sentinel;
    CHECK(!cb_imu_math_decode(raw, 8, 100, &out));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);

    out = sentinel;
    CHECK(!cb_imu_math_decode(raw, 0, 512, &out));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);

    out = sentinel;
    CHECK(!cb_imu_math_decode(raw, 8, 15, &out));
    CHECK(memcmp(&sentinel, &out, sizeof(out)) == 0);
}

static void test_imu_math_decode_int16_min(void)
{
    uint8_t raw[CB_IMU_RAW_LEN];
    cb_imu_sample_t out;

    imu_zero_buf(raw);
    imu_put_le16(raw, 2, INT16_MIN);
    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(-8.0f, out.ax_g);

    imu_zero_buf(raw);
    imu_put_le16(raw, 8, INT16_MIN);
    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(-512.0f, out.gx_dps);

    imu_zero_buf(raw);
    imu_put_le16(raw, 0, INT16_MIN);
    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(-128.0f, out.temp_c);

    imu_zero_buf(raw);
    imu_put_le16(raw, 2, -1);
    CHECK(cb_imu_math_decode(raw, 8, 512, &out));
    CHECK_CLOSE(-8.0f / 32768.0f, out.ax_g);
}

static void test_message_validation_and_retention(void)
{
    beeper_model_t model;
    beeper_model_init(&model);
    CHECK(!beeper_text_valid(NULL));
    CHECK(!beeper_text_valid(""));
    CHECK(!beeper_text_valid(" \t\n"));
    CHECK(!beeper_text_valid("\xc0\xaf")); /* overlong UTF-8 */
    CHECK(!beeper_text_valid("\xed\xa0\x80")); /* surrogate */
    CHECK(!beeper_text_valid("\xf4\x90\x80\x80")); /* above Unicode range */
    CHECK(!beeper_text_valid("\xe2\x82")); /* incomplete */
    CHECK(!beeper_text_valid("hello\x01"));
    CHECK(beeper_text_valid("Hola \xc3\xb1\nHello"));
    char text[BEEPER_TEXT_MAX + 2];
    memset(text, 'a', sizeof(text));
    text[BEEPER_TEXT_MAX + 1] = 0;
    CHECK(!beeper_model_message(&model, text, false, 0));
    text[BEEPER_TEXT_MAX] = 0;
    CHECK(beeper_model_message(&model, text, false, 0));
    CHECK(model.unread);
    CHECK_INT(1, model.notification_id);
    CHECK_INT(0, model.messages[0].epoch);
    beeper_model_ack(&model);
    CHECK(!model.unread);
    for (unsigned i = 0; i < BEEPER_HISTORY + 3; ++i)
        CHECK(beeper_model_message(&model, "reply", true, BEEPER_CLOCK_MIN));
    CHECK_INT(BEEPER_HISTORY, model.count);
    CHECK_INT(5, model.messages[0].id);
    CHECK_INT(20, model.messages[BEEPER_HISTORY - 1].id);
    CHECK(!model.unread);
    CHECK_INT(1, model.notification_id);
}

static void test_daily_alarm_and_clock_changes(void)
{
    beeper_model_t model;
    beeper_model_init(&model);
    model.alarm.enabled = true;
    model.alarm.hour = 1;
    model.alarm.minute = 30;
    model.utc_offset_minutes = 90;
    CHECK(!beeper_model_tick(&model, 0, 100)); /* no clock */
    CHECK(beeper_model_tick(&model, BEEPER_CLOCK_MIN, 1000));
    CHECK_INT(61000, model.ringing_until_ms);
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN + 10, 11000));
    beeper_model_ack(&model);
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN + 20, 21000)); /* dismiss stays quiet */
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN - 60, 22000)); /* backwards */
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN, 23000));
    CHECK(beeper_model_tick(&model, BEEPER_CLOCK_MIN + 86400, 90000));
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN + 86460, 150000));
    CHECK_INT(0, model.ringing_until_ms); /* auto stop */
    model.alarm.enabled = false;
    CHECK(!beeper_model_tick(&model, BEEPER_CLOCK_MIN + 172800, 160000));
    model.alarm.enabled = true;
    model.utc_offset_minutes = -30;
    model.alarm.hour = 23;
    model.alarm.minute = 30;
    CHECK(beeper_model_tick(&model, BEEPER_CLOCK_MIN + 172800, 170000)); /* local previous day */
}

static void test_heartbeat_double_pulse(void)
{
    CHECK_INT(0, beeper_heartbeat_mix(0));
    CHECK_INT(255, beeper_heartbeat_mix(180));
    CHECK_INT(0, beeper_heartbeat_mix(360));
    CHECK_INT(255, beeper_heartbeat_mix(640));
    CHECK_INT(0, beeper_heartbeat_mix(820));
    CHECK_INT(0, beeper_heartbeat_mix(1400));
    for (unsigned i = 1; i < 180; ++i)
        CHECK(beeper_heartbeat_mix(i) >= beeper_heartbeat_mix(i - 1));
}

static void test_expansion_gpio_conflicts(void)
{
    design_hardware_t h = {
        .buttons = {{2, DESIGN_NEXT_REPLY, true}, {4, DESIGN_SEND_REPLY, true},
                    {5, DESIGN_ACK, true}, {6, DESIGN_EDIT_ALARM, true}},
        .motor_gpio = 7, .speaker = true, .microphone = true,
        .bclk = 8, .ws = 9, .dout = 10, .din = 11,
    };
    CHECK(design_hardware_valid(&h));
    h.din = h.motor_gpio;
    CHECK(!design_hardware_valid(&h));
    h.din = 11;
    for (int gpio = 0; gpio <= 48; ++gpio) {
        CHECK_INT(gpio == 2 || (gpio >= 4 && gpio <= 11), design_gpio_available(gpio));
    }
    h.motor_gpio = CB_PIN_LCD_BL;
    CHECK(!design_hardware_valid(&h));
    h.motor_gpio = -1;
    h.buttons[1].gpio = h.buttons[0].gpio;
    CHECK(!design_hardware_valid(&h));
    for (size_t i = 0; i < DESIGN_MAX_BUTTONS; ++i) h.buttons[i].gpio = -1;
    h.speaker = h.microphone = false;
    CHECK(design_hardware_valid(&h)); /* Disabled channels never claim pins. */
}

static void test_designed_effect_boundaries(void)
{
    design_step_t steps[] = {
        {.duration_ms=100, .color=0x0000ff, .brightness=100, .frequency=440,
         .screen=true, .screen_color=0, .blend=true, .wave=DESIGN_SINE},
        {.duration_ms=200, .color=0xffff00, .brightness=50, .vibration=100,
         .frequency=880, .volume=20, .screen=true, .screen_color=0xffffff},
    };
    design_sequence_t sequence = {steps, 2, true};
    design_effect_t effect;
    design_effect(&sequence, 50, &effect);
    CHECK_INT(0x7f7f80, effect.color);
    CHECK_INT(50, effect.vibration);
    CHECK_INT(660, effect.frequency);
    CHECK_INT(10, effect.volume);
    CHECK_INT(75, effect.brightness);
    CHECK_INT(0x7f7f7f, effect.screen_color);
    design_effect(&sequence, 100, &effect);
    CHECK_INT(0xffff00, effect.color);
    design_effect(&sequence, 300, &effect);
    CHECK_INT(0x0000ff, effect.color);
    sequence.loop = false;
    design_effect(&sequence, 300, &effect);
    CHECK_INT(0, effect.vibration);
    CHECK_INT(0, effect.volume);
    CHECK_INT(0, effect.brightness);
    CHECK(!effect.screen);
    design_effect(&sequence, UINT64_MAX, &effect);
    CHECK_INT(0, effect.volume);
    steps[0].duration_ms = steps[1].duration_ms = 0;
    design_effect(&sequence, 0, &effect);
    CHECK_INT(0, effect.brightness);
}

static void test_designed_animation_pose(void)
{
    const design_key_t keys[] = {{0, -10, 0, 255}, {100, 10, 20, 55}};
    design_layer_t layer = {.x=8, .y=12, .opacity=200, .period_ms=200,
                            .keys=keys, .key_count=2};
    design_pose_t pose;
    design_pose(&layer, 50, &pose);
    CHECK_INT(0, pose.x);
    CHECK_INT(10, pose.y);
    CHECK_INT(155, pose.opacity);
    design_pose(&layer, 150, &pose);
    CHECK_INT(10, pose.x);
    design_pose(&layer, 200, &pose);
    CHECK_INT(-10, pose.x);
    layer.key_count = 0;
    design_pose(&layer, 100, &pose);
    CHECK_INT(8, pose.x);
    CHECK_INT(12, pose.y);
    CHECK_INT(200, pose.opacity);
}

static void test_designed_gif_timing(void)
{
    const uint32_t durations[] = {50, 100, 200};
    CHECK_INT(0, design_frame(durations, 3, 49));
    CHECK_INT(1, design_frame(durations, 3, 50));
    CHECK_INT(1, design_frame(durations, 3, 149));
    CHECK_INT(2, design_frame(durations, 3, 150));
    CHECK_INT(0, design_frame(durations, 3, 350));
    CHECK_INT(0, design_frame(NULL, 0, 1));
}

int main(void)
{
    run_test(test_pins_lcd, "pins LCD and RGB contract");
    run_test(test_pins_sensors_battery, "pins I2C, QMI, BOOT and battery contract");
    run_test(test_pins_sd_and_uniqueness, "pins SD contract and GPIO uniqueness");
    run_test(test_qmi_registers, "QMI8658 register map");
    run_test(test_qmi_fields_and_ranges, "QMI8658 masks and ranges");
    run_test(test_qmi_odr_and_lpf, "QMI8658 ODR and LPF encodings");
    run_test(test_duty_clamp, "backlight duty clamp and floor rounding");
    run_test(test_duty_custom_maxima, "backlight custom maxima and wide multiply");
    run_test(test_little_endian_decode, "little-endian signed decode");
    run_test(test_accel_scales, "accelerometer scale conversion");
    run_test(test_gyro_scales, "gyroscope scale conversion");
    run_test(test_qmi_temperature, "QMI8658 signed temperature conversion");
    run_test(test_adc_calibrated, "calibrated ADC millivolts");
    run_test(test_adc_fallback_ratio, "uncalibrated ADC ratio and clamp");
    run_test(test_fsm_init_and_idle, "button init and idle");
    run_test(test_fsm_debounce_and_bounce, "button debounce and bounce restart");
    run_test(test_fsm_single_click_deferred, "button deferred single click");
    run_test(test_fsm_double_click, "button double click without single click");
    run_test(test_fsm_double_gap_boundary, "button double gap exact boundary");
    run_test(test_fsm_gap_expiry_produces_singles, "button expired gap produces singles");
    run_test(test_fsm_no_press_duration_dead_zone, "button has no 350 ms press dead zone");
    run_test(test_fsm_long_press, "button long press once and release suppression");
    run_test(test_fsm_long_cancels_pending_click, "button long press cancels pending click");
    run_test(test_fsm_no_duplicate_edges, "button does not duplicate stable edges");
    run_test(test_fsm_time_wraparound, "button injected-time wraparound");
    run_test(test_fsm_fully_polled_traces, "button fully polled multi-gesture traces");
    run_test(test_pattern_palette, "pattern palette and out-of-range index");
    run_test(test_pattern_solid_and_invalid, "pattern solid fill and invalid input");
    run_test(test_pattern_corner_markers, "pattern exact corner markers");
    run_test(test_pattern_bar_boundaries, "pattern bar boundaries and remainders");
    run_test(test_pattern_ramp, "pattern grey ramp endpoints and rows");
    run_test(test_pattern_stride_and_endian, "pattern packed stride, guards and endian");
    run_test(test_pattern_odd_geometry, "pattern odd-size geometry");
    run_test(test_rgb_dim_cap_macro, "rgb dim cap percent contract");
    run_test(test_rgb_math_dim, "rgb dim peak-scale, idempotence and NULL safety");
    run_test(test_battery_macro_contract, "battery divider/gain/window macro contract");
    run_test(test_battery_math_conversion, "battery mV conversion, clamp and saturation");
    run_test(test_battery_math_plausible, "battery plausibility inclusive bounds");
    run_test(test_imu_math_raw_len, "imu burst length contract");
    run_test(test_imu_math_decode_resting, "imu decode resting vector");
    run_test(test_imu_math_decode_accel_sign, "imu decode accel signs");
    run_test(test_imu_math_decode_gyro, "imu decode gyro values");
    run_test(test_imu_math_decode_ranges, "imu decode per-range scaling");
    run_test(test_imu_math_decode_temp, "imu decode temperature");
    run_test(test_imu_math_decode_layout, "imu decode burst layout");
    run_test(test_imu_math_decode_invalid, "imu decode invalid input, out untouched");
    run_test(test_imu_math_decode_int16_min, "imu decode INT16_MIN without UB");
    run_test(test_message_validation_and_retention, "message UTF-8 validation and bounded history");
    run_test(test_daily_alarm_and_clock_changes, "daily alarm, offset, dismissal and clock changes");
    run_test(test_heartbeat_double_pulse, "blue-yellow heartbeat double pulse");
    run_test(test_expansion_gpio_conflicts, "expansion pins exclude board wiring and duplicate assignments");
    run_test(test_designed_effect_boundaries, "designed effects interpolate, loop and stop all outputs");
    run_test(test_designed_animation_pose, "designed position and opacity keyframes");
    run_test(test_designed_gif_timing, "variable GIF frame durations and loop boundaries");

    printf("1..%d\n", tests_run);
    printf("# failed %d\n", tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
