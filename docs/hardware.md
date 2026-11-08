# Hardware reference — Waveshare ESP32-S3-LCD-1.47B

> Status (updated 2026-11-08, board attached): chip identity, flash size and PSRAM presence are
> now **hardware-verified** (see below); every peripheral behaviour on this page is still
> **build-verified only** until the checklist in [verification.md](verification.md) ticks it.
> Pin values mirror the frozen header `components/cb_bsp/include/cb_pins.h`.

## Board / SoC

- **ESP32-S3R8**: dual-core Xtensa LX7 @ 240 MHz, 512 KB SRAM, Wi-Fi 2.4 GHz b/g/n, BLE 5, PCB antenna.
- **16 MB flash** (QIO), **8 MB PSRAM — octal (OPI) mode @ 80 MHz**. Octal mode is mandatory:
  `CONFIG_SPIRAM_MODE_OCT=y` (wrong mode = boot loop / 0 PSRAM — see
  [troubleshooting.md](troubleshooting.md)).
- 1.47" **172×320 ST7789** LCD, **no touch panel**, 1× addressable RGB LED, BOOT button, battery
  ADC, TF (microSD) slot, native **USB-Serial/JTAG** on Type-C (no UART bridge chip).
- Input model (no touch): BOOT button, IMU tilt/gestures (later items), radio (later items).

**Observed device identity — hardware-verified 2026-11-08** (`esptool flash_id` on
`/dev/ttyACM0`): `ESP32-S3 QFN56 rev v0.2` · features: `Embedded PSRAM 8MB (AP_3v3)` ·
`Detected flash size: 16MB`. With this config the root app boots, streams a 2 s heartbeat over
the USB-Serial/JTAG console, and soaked ~11 min (`alive #323`) with no WDT/reset — see
[verification.md](verification.md) "Hardware status".

## Pin table (single source of truth: `cb_pins.h` — do not renumber anywhere else)

| Function | Signal | GPIO | Frozen macro |
|---|---|---|---|
| LCD (SPI2 host) | SCLK | 40 | `CB_PIN_LCD_SCLK` |
| | MOSI (SDA) | 45 | `CB_PIN_LCD_MOSI` |
| | CS | 42 | `CB_PIN_LCD_CS` |
| | DC | 41 | `CB_PIN_LCD_DC` |
| | RST | 39 | `CB_PIN_LCD_RST` |
| | Backlight (LEDC PWM, on-level 1) | 46 | `CB_PIN_LCD_BL` |
| | MISO | not connected | — |
| IMU QMI8658 (I2C, 400 kHz) | SCL | 47 | `CB_PIN_I2C_SCL` |
| | SDA | 48 | `CB_PIN_I2C_SDA` |
| RGB LED (WS2812-compatible, GRB) | DIN | 38 | `CB_PIN_RGB` |
| BOOT button (active low, pull-up) | — | 0 | `CB_PIN_BOOT_BTN` |
| Battery sense (ADC1_CH0, atten 12 dB) | — | 1 | `CB_PIN_BAT_ADC` |
| TF card (SDMMC) | CLK | 14 | `CB_SD_PIN_CLK` |
| | CMD | 15 | `CB_SD_PIN_CMD` |
| | D0 | 16 | `CB_SD_PIN_D0` |
| | D1 | 18 | `CB_SD_PIN_D1` |
| | D2 | 17 | `CB_SD_PIN_D2` |
| | D3 | 21 | `CB_SD_PIN_D3` |

**Reserved / unusable GPIO:** 19/20 (USB D−/D+) and 26–37 (flash + octal PSRAM). The RST button
is hardware-only (not a readable GPIO).

Other frozen constants: `CB_LCD_SPI_HOST 1` (= `SPI2_HOST`; the skeleton-verified bus),
`CB_LCD_PIXEL_CLK_HZ (40*1000*1000)`, `CB_LCD_BL_PWM_HZ 4000`, `CB_I2C_PORT 0`,
`CB_I2C_FREQ_HZ 400000`, `CB_QMI_ADDR_0 0x6A`, `CB_QMI_ADDR_1 0x6B`, `CB_QMI_WHO_AM_I_VAL 0x05`,
`CB_BAT_ADC_UNIT 0` (= `ADC_UNIT_1`), `CB_BAT_ADC_CHANNEL 0`, `CB_BAT_DIVIDER_NUM 3`,
`CB_BAT_DIVIDER_DEN 1`, `CB_BAT_CAL_GAIN_NUM 700`, `CB_BAT_CAL_GAIN_DEN 695`.

## Strapping-pin warnings (read before using GPIO0/45/46)

- **GPIO0 (BOOT)**: strapping pin. Holding BOOT while resetting enters **download mode** — this is
  the flash-recovery path ([flashing.md](flashing.md)), and it is why the user button doubles as a
  boot-mode control. Software must never drive it push-pull.
- **GPIO45 (LCD MOSI)**: strapping pin (VDD_SPI voltage select). Sampled at boot only; driving it
  as SPI MOSI afterwards is the vendor design and works — but expect it to be an input at reset.
- **GPIO46 (backlight)**: strapping pin (ROM messages / boot mode related, no internal pull-down).
  The backlight may **flicker briefly at boot** regardless of software; the BSP starts the PWM at
  duty 0 (item 2) to keep the flash-on-boot as short as possible.

## LCD (ST7789, 172×320)

- The ST7789 GRAM is 240 px wide; this panel is 172 px → the driver must call
  `esp_lcd_panel_set_gap(panel, 34, 0)` (`CB_LCD_X_GAP 34`, `CB_LCD_Y_GAP 0`). Without the gap the
  image is shifted/cropped (hardware-day check #4).
- Colour inversion **on** (`esp_lcd_panel_invert_color(panel, true)`), element order RGB, 16 bpp
  RGB565 with byte swap in the LVGL pipeline — the exact combination of the verified skeleton:
  SPI2 @ 40 MHz, panel-io CS 42 / DC 41, SPI mode 0, 8-bit cmd/param, queue depth 10; LEDC 4 kHz
  10-bit backlight (duty max 1023, `CB_MATH_BACKLIGHT_MAX`).
- LVGL 9.5 via `esp_lvgl_port` 2.9.0: double buffer 172×40 px, `buff_dma` + `buff_spiram`,
  `swap_bytes` (PSRAM/DMA runtime behaviour is a hardware-day item).

## IMU (QMI8658, I2C)

- Candidate addresses **0x6A and 0x6B** — the vendor wiki says 0x6A, the vendor code uses 0x6B, so
  the driver (item 4) **probes both** with `i2c_master_probe()` and logs the winner.
- WHO_AM_I register 0x00, expected value **0x05** (mismatch = WARN, not hard fail; several chip
  revisions circulate). 6-axis accel+gyro+temperature; register block frozen in
  `cb_qmi8658_regs.h` (see [api.md](api.md)).

## Battery sense

- GPIO1 = **ADC1** channel 0 (`CB_BAT_ADC_UNIT 0` = `ADC_UNIT_1`), 12 dB attenuation, oneshot +
  eFuse calibration when available (item 3).
- Conversion (frozen, applied by the **battery driver**, never inside `cb_math`):
  `V = cali_mv × CB_BAT_DIVIDER_NUM/CB_BAT_DIVIDER_DEN × CB_BAT_CAL_GAIN_NUM/CB_BAT_CAL_GAIN_DEN / 1000`
  i.e. `×3` divider and `×700/695` gain (the vendor's "divide by 0.992857"). No fuel gauge exists —
  voltage only; percentages are deferred until hardware calibration.
- Without a cell inserted (USB-only), the reading can be implausible — the driver WARNs (item 3).

## TF card (SDMMC)

- Pins as tabled; **no card-detect and no write-protect lines** — software cannot always
  distinguish "no card" from a mount error.
- Item 5 drives it 4-bit @ 20 MHz with automatic **1-bit fallback** (logged). The vendor demo
  shipped 1-bit although all four data lines are wired; internal pull-ups only (the vendor comments
  note external 10 k pull-ups are recommended for reliability — hardware-day item).
- **`format_if_mount_failed` is always false.** No code path may silently format a card (enforced by
  `audit-static`).

## RGB LED

- Single WS2812-compatible pixel on GPIO38, GRB order, driven by `espressif/led_strip` 3.1.1
  (RMT backend, 10 MHz). The LED sits behind acrylic → brightness is capped low (item 3).

## USB / power

- Type-C = native **USB-Serial/JTAG**: logs and flashing share this port
  (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in the board defaults; Arduino `CDCOnBoot=cdc` +
  `USBMode=hwcdc`). UART0 (GPIO43/44) is not broken out.
- 3.7 V Li-ion header with on-board charge management (USB-C charges); no charge-status GPIO is
  exposed to software.
