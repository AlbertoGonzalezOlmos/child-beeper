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

    printf("1..%d\n", tests_run);
    printf("# failed %d\n", tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
