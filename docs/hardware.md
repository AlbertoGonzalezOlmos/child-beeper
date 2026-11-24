# Hardware reference — Waveshare ESP32-S3-LCD-1.47B

> Status (updated for item 5): chip identity, flash size, PSRAM presence, console and app boot are
> **hardware-verified**. Item-2 display, item-3 RGB/button/battery and item-4 IMU serial checks are
> also recorded. Item-5 SDMMC behavior below is the frozen design contract until the coordinator
> captures a card run; card presence is unknown and no mounted-path fact is claimed. Pin values
> mirror `components/cb_bsp/include/cb_pins.h`; measurements live in [verification.md](verification.md).

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

## BOOT button wrapper (item 3)

- GPIO0 remains an active-low input with pull-up, never a software-driven output. The BSP polls it
  every **10 ms** from one esp_timer and feeds the existing `cb_button_fsm`; debounce 20 ms, deferred
  click gap 350 ms and long press 800 ms are not reimplemented in the wrapper.
- Callback registration always stores the callback/user pair and returns `ESP_OK`, before or after
  init and even if the poller failed. Pair replacement is protected by a critical section, while the
  callback runs outside it in esp_timer task context. Callbacks must be short and queue blocking
  work. Physical gestures and recovery-button operation remain human checklist items.
- **Serial-verified 2026-11-23**: `I (588) cb_button: poller up, 10 ms`. This proves the poller
  started; no CLICK/DOUBLE_CLICK/LONG_PRESS gesture was observed, so gesture timing remains
  hardware-PENDING (needs human).

## LCD (ST7789, 172×320) — bring-up facts (item 2; serial init/render observed 2026-11-23)

- The ST7789 GRAM is 240 px wide; this panel is 172 px → the driver must call
  `esp_lcd_panel_set_gap(panel, 34, 0)` (`CB_LCD_X_GAP 34`, `CB_LCD_Y_GAP 0`). Without the gap the
  image is shifted/cropped (hardware-day check).
- Frozen panel bring-up order (`cb_lcd_init`, from the verified skeleton): SPI2 bus @ 40 MHz
  (SCLK 40 / MOSI 45, `max_transfer_sz = CB_LCD_H_RES*80*2`) → `esp_lcd_new_panel_io_spi`
  (CS 42 / DC 41, SPI mode 0, 8-bit cmd/param, queue depth 10) → `esp_lcd_new_panel_st7789`
  (RST 39, `rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`, 16 bpp) → `reset` → `init` →
  `set_gap(34,0)` → `invert_color(true)` → `disp_on_off(true)`. The enum binding is guarded by
  `_Static_assert(CB_LCD_SPI_HOST == SPI2_HOST)`.
- Colour inversion **on** + element order RGB + `swap_bytes` in the LVGL flush path are **one
  exact combination** — the diagnostic screen separates the failure axes (red↔blue = element
  order; washed-out black = inversion; 34-px shift/crop = gap; missing corners = mirroring).
- Backlight: LEDC 4 kHz (`CB_LCD_BL_PWM_HZ`), 10-bit (duty max 1023, `CB_MATH_BACKLIGHT_MAX`),
  on-level 1. **Init duty is 0** — no white boot flash (GPIO46 strapping); `cb_lcd` turns it on
  to `CB_BACKLIGHT_ON_PERCENT` (100) via a one-shot `LV_EVENT_REFR_READY` hook **after the first
  frame is drawn** (ruling A2 — examples must not switch it on themselves).
- LVGL 9.5 via `esp_lvgl_port` 2.9.0 with **unmodified** `ESP_LVGL_PORT_INIT_CONFIG()` defaults
  (A7, verified against the 2.9.0 header): task priority **4**, task stack **7168**,
  `task_max_sleep_ms 500`, `timer_period_ms 5`. Draw buffers (**hardware-measured recipe**, commit
  92e336d): two **internal DMA-capable** RGB565 buffers of `CB_LCD_H_RES*40` = 172×40 px each
  (2 × 13,760 B = 27,520 B internal SRAM), `buff_dma=true`, **`buff_spiram=false`** — the port
  rejects `buff_dma + buff_spiram` with `ESP_ERR_NOT_SUPPORTED` on this SoC — `swap_bytes=true`,
  and `io_handle` passed to `lvgl_port_add_disp` (assert at `esp_lvgl_port_disp.c:120`).
  Examples allocate full-frame canvases (172×320×2 = 110080 B) with
  `heap_caps_malloc(…, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)` — never as a static array (A10).

## IMU (QMI8658, I2C)

- **Bus**: new `driver/i2c_master.h` only. Port 0, SCL47, SDA48, 400 kHz,
  `I2C_CLK_SRC_DEFAULT`, `glitch_ignore_cnt=7`, internal pull-ups **enabled** and logged as
  `i2c: internal pull-ups enabled`. Internal pull-up strength/signal integrity is
  **hardware-PENDING**, not proven by the log.
- **Probe/identity**: `i2c_master_probe()` tries **0x6A then 0x6B** with a bounded 50 ms timeout
  and logs the winner. If neither answers, init returns `ESP_ERR_NOT_FOUND`; the example enters its
  alive-only degraded path. WHO_AM_I register 0x00 is read next; expected **0x05**. A mismatch
  produces a WARN and `ESP_ERR_INVALID_RESPONSE`, never a process abort; the optional part is
  suppressed by `cb_bsp`. **Measured 2026-11-23**: 0x6A was silent, 0x6B answered, WHO_AM_I was
  **0x05** and revision readback was **0x7C** ([verification.md](verification.md) checklist 8).
- **Configuration order**: CTRL1 `CB_QMI_CTRL1_ADDR_AI` (**0x40**, bit 6) to make burst reads
  advance, then the required vendor CH9/CTRL9 `CB_QMI_CTRL_CMD_AHB_CLOCK_GATING` command (**0x12**)
  with a bounded 100 ms STATUSINT poll, before CTRL2 accel **±8 g** + selected ODR (120 Hz range/ODR
  bits 0x26), CTRL3 gyro **±512 dps** + selected ODR (120 Hz range/ODR bits 0x56), preserving
  unmasked bits in both RMWs; CTRL5 **LPF mode 0/bypass** using complement-mask RMW, then CTRL7
  `CB_QMI_CTRL7_ACC_EN | CB_QMI_CTRL7_GYR_EN` (**0x03**) to enable both sensors. **Measured
  2026-11-23** (fixed in `a05fded`): 0x80 made a burst repeat its first byte, 0x40 returned real
  data, and omitting CTRL9 0x12 made the output registers return a moving counter
  ([verification.md](verification.md) checklist 8). ODR choice symbols are `CB_IMU_ODR_120`
  (`CONFIG_CB_IMU_ODR_120=y` by default), `CB_IMU_ODR_250` and `CB_IMU_ODR_1000`.
- **Burst**: one bounded `i2c_master_transmit_receive` starts at `CB_QMI_TEMP_L` (0x33) and reads
  `CB_IMU_RAW_LEN` (**14**) bytes through `CB_QMI_GZ_H` (0x40): TEMP, AX, AY, AZ, GX, GY, GZ,
  little-endian int16. Accel/gyro scale is raw×range/32768; temperature is signed raw/256 °C.
- **Concurrency**: one mutex serializes WHO/configuration transfers and sample reads with a
  **100 ms** acquisition bound. A busy sample read returns `ESP_ERR_TIMEOUT` and its WARN is
  first-event-allowed and rate-limited to ≤1 Hz with race-free last-log state. The driver emits no
  per-read data line; `04_imu` owns the 2 s sample output.
- Resting acceptance for the example/README is max |axis| **0.9–1.1 g**, other axes **<0.15 g**,
  |gyro| **<2.00 dps**, temperature **10–50 °C**. Full-scale accuracy, axis orientation, deliberate
  motion and pull-up electrical quality remain human/hardware checks.

## Battery sense

- GPIO1 = **ADC1** channel 0 (`CB_BAT_ADC_UNIT 0` = `ADC_UNIT_1`), 12 dB attenuation, 12-bit
  oneshot, `ADC_DIGI_CLK_SRC_DEFAULT` and `ADC_ULP_MODE_DISABLE`. Static assertions bind the frozen
  macros to `ADC_UNIT_1` and `ADC_CHANNEL_0`.
- ESP32-S3 eFuse calibration uses `adc_cali_curve_fitting_config_t` (including the channel) and
  `adc_cali_create_scheme_curve_fitting()`. If calibration creation fails, the driver logs one WARN
  and keeps the ADC running through `cb_math_adc_raw_to_mv()`; it does not fail the optional part.
- Conversion (frozen, applied by the **battery driver/math module**, never inside `cb_math`):
  `V = pin_mv × CB_BAT_DIVIDER_NUM/CB_BAT_DIVIDER_DEN × CB_BAT_CAL_GAIN_NUM/CB_BAT_CAL_GAIN_DEN / 1000`.
  The integer-mV helper returns `pin_mv × 3 × 700 / 695` with an int64 intermediate and no `/1000`;
  negative input returns 0 and 4095 mV at the pin produces 12373 mV at the battery node. A
  `cb_battery_sample_t` reports raw, pin_mv, battery_mv, calibrated and plausible together.
- The inclusive **2500–4500 mV** plausibility window is only a heuristic. On transition to
  implausible the driver logs `W cb_battery: implausible %ld mV (no cell inserted?)`; recovery logs
  `I cb_battery: reading plausible again`. It emits no per-read data line and stable polling cannot
  repeat the warning. A floating no-cell input may still land inside the plausible band. There is
  no fuel gauge or percentage API; ±5% voltage accuracy requires a human multimeter measurement.
- **Serial-verified 2026-11-23**: init printed `cb_battery: adc up, cali curve`, followed by
  `battery: raw=1638 pin_mv=1397 battery_mv=4221 calibrated=1 plausible=1` and
  `battery: raw=1637 pin_mv=1396 battery_mv=4218 calibrated=1 plausible=1`. This proves curve-fitting
  calibration and a stable plausible sample on this silicon, not multimeter accuracy or whether a
  cell is physically fitted. `W battery: read failed: %s` is the one-shot error contract; polling
  then stops while `battery: alive #%u` continues.

## TF card (SDMMC)

- Pins as tabled; **no card-detect and no write-protect lines** — software cannot distinguish a
  missing card from an unreadable/non-FAT card, poor contact, width mismatch or another mount error.
  **A mount error is not proof that no card exists.**
- The item-5 first attempt is 4-bit at a **configured maximum** of 20 MHz
  (`CB_SD_MAX_FREQ_KHZ=20000`), using SDMMC slot 1, the six frozen pins and
  `SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_DEINIT_ARG`. `max_freq_khz` is not a measured clock.
- The slot overrides the IDF ESP32-S3 default pins, has width 4 and
  `SDMMC_SLOT_FLAG_INTERNAL_PULLUP` (exact IDF 6.1 name), with no CD/WP. Internal pull-ups only;
  external 10 k pull-ups are vendor-recommended for reliability, so signal integrity remains a
  hardware-day check. There is no separate SD pull-up serial line.
- On failure the driver logs `4-bit mount failed: %s — retrying 1-bit`, calls
  `sdmmc_host_deinit()`, and retries once with `SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG`
  and slot width 1. A successful line reports the negotiated width from `log_bus_width`. A 4-bit
  success leaves fallback unverified; a 1-bit success is valid fallback evidence only with the
  preceding failure/fallback log.
- Exactly one mount helper/site has `format_if_mount_failed = false`, `max_files = 5`, and
  `allocation_unit_size = 16 * 1024`. This is a source-review invariant backed by the existing
  `format_if_mount_failed = true` forbidden-construct audit, not permission to test formatting on
  an unknown card. No format/mkfs path exists.
- `cb_sd_info()` reports FAT total/free **bytes**. Unmount uses
  `esp_vfs_fat_sdcard_unmount()` then `sdmmc_host_deinit()`; only success clears mounted/width.
  Card presence, capacity, actual width, read/write/verify/delete and fallback all remain
  **PENDING** until the coordinator supplies a mounted-card capture.

## RGB LED

- Single WS2812-compatible pixel on GPIO38, GRB order, driven by `espressif/led_strip` 3.1.1
  with `.color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_GRB`, `RMT_CLK_SRC_DEFAULT`, and a
  10 MHz RMT resolution. Init clears the pixel through set+refresh and logs
  `I cb_rgb: rmt up gpio38, cap 20%`.
- The pure cap is **20% of full scale, peak 51**: values whose peak is at most 51 are unchanged;
  above that, every channel is multiplied by 51/peak with floor. This preserves hue and is
  idempotent; it is not independent per-channel clipping or uniform scaling of already-dim values.
- Set and refresh are one mutex-protected transaction with a 100 ms acquisition timeout. A timeout
  returns `ESP_ERR_TIMEOUT` and logs at most once per second. Pre-init errors are one-shot and
  set failures are rate-limited to 1 Hz; last-log state is updated under a critical section. The
  LED sits behind acrylic, so actual colour/brightness/brownout behaviour still needs human/hardware
  observation.
- **Serial-verified 2026-11-23**: init cleared the pixel and printed
  `I cb_rgb: rmt up gpio38, cap 20%`; the example then announced all five post-dim steps:
  `cycle 1 r=51 g=0 b=0`, `cycle 2 r=0 g=51 b=0`, `cycle 3 r=0 g=0 b=51`,
  `cycle 4 r=51 g=51 b=51`, and `cycle 5 r=0 g=0 b=0`. This proves the numeric 20% cap and RMT
  transaction sequence, not visible colour/brightness. `W rgb_button: cycle %u failed: %s` is now
  emitted only on the first failure of a failure streak, with the gate cleared by a successful
  cycle.

## USB / power

- Type-C = native **USB-Serial/JTAG**: logs and flashing share this port
  (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in the board defaults; Arduino `CDCOnBoot=cdc` +
  `USBMode=hwcdc`). UART0 (GPIO43/44) is not broken out.
- 3.7 V Li-ion header with on-board charge management (USB-C charges); no charge-status GPIO is
  exposed to software.
