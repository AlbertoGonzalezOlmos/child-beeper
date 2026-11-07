#ifndef CB_PINS_H
#define CB_PINS_H

/* Waveshare ESP32-S3-LCD-1.47B fixed board wiring. Pure C89-safe macros. */

#define CB_PIN_LCD_SCLK 40
#define CB_PIN_LCD_MOSI 45
#define CB_PIN_LCD_CS 42
#define CB_PIN_LCD_DC 41
#define CB_PIN_LCD_RST 39
#define CB_PIN_LCD_BL 46
#define CB_LCD_H_RES 172
#define CB_LCD_V_RES 320
#define CB_LCD_X_GAP 34
#define CB_LCD_Y_GAP 0
#define CB_LCD_SPI_HOST 1       /* spi_host_device_t: SPI2_HOST == 1 */
#define CB_LCD_PIXEL_CLK_HZ (40*1000*1000)
#define CB_LCD_BL_PWM_HZ 4000

#define CB_PIN_RGB 38

#define CB_PIN_I2C_SCL 47
#define CB_PIN_I2C_SDA 48
#define CB_I2C_PORT 0
#define CB_I2C_FREQ_HZ 400000
#define CB_QMI_ADDR_0 0x6A
#define CB_QMI_ADDR_1 0x6B
#define CB_QMI_WHO_AM_I_VAL 0x05

#define CB_PIN_BOOT_BTN 0

#define CB_PIN_BAT_ADC 1
#define CB_BAT_ADC_UNIT 0       /* adc_unit_t: ADC_UNIT_1 == 0 */
#define CB_BAT_ADC_CHANNEL 0
#define CB_BAT_DIVIDER_NUM 3
#define CB_BAT_DIVIDER_DEN 1    /* vendor: * 3.0 */
#define CB_BAT_CAL_GAIN_NUM 700
#define CB_BAT_CAL_GAIN_DEN 695 /* vendor divides by 0.992857 */
/* Battery volts = cali_mv * DIVIDER_NUM/DIVIDER_DEN
 *                 * CAL_GAIN_NUM/CAL_GAIN_DEN / 1000. */

#define CB_SD_PIN_CLK 14
#define CB_SD_PIN_CMD 15
#define CB_SD_PIN_D0 16
#define CB_SD_PIN_D1 18
#define CB_SD_PIN_D2 17
#define CB_SD_PIN_D3 21

#endif /* CB_PINS_H */
