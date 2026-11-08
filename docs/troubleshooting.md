# Troubleshooting & known pitfalls

> Status: build-verified (item 1, 2026-11-08); measured facts from the vendor demo's failed
> IDF-6.1 build, the verified skeleton, and this machine's toolchain — per the rule that docs cite measured evidence,
> not wiki prose. Symptoms marked *(hardware)* can only be confirmed on the board — see
> [verification.md](verification.md).

## Why the vendor demo does not build on IDF v6.1 (measured)

The official Waveshare `ESP32-S3-LCD-1.47B-Test` project is reference-only. Compiled against IDF
v6.1 it fails with:

1. **Legacy I2C API is gone.** `driver/i2c.h` no longer exists (`i2c_param_config`,
   `i2c_driver_install`, `i2c_master_write_to_device` removed). Use `driver/i2c_master.h`:
   `i2c_new_master_bus`, `i2c_master_bus_add_device`, `i2c_master_probe`, `i2c_master_transmit`,
   `i2c_master_transmit_receive`, `i2c_master_receive`.
2. **LCD enum renamed.** `lcd_color_rgb_endian_t` / `LCD_RGB_ENDIAN_RGB|BGR` →
   `lcd_rgb_element_order_t` / `LCD_RGB_ELEMENT_ORDER_RGB|BGR`.
3. **led_strip 2.5.5 broken.** `led_strip_spi_dev.c` fails on undeclared `MALLOC_CAP_*` /
   `heap_caps_calloc` — IDF 6 removed the transitive include chain (`esp_heap_caps.h` must be
   included explicitly). This repo pins **led_strip 3.1.1**; never downgrade to 2.x.
4. **Missing transitive includes generally.** Vendor code relying on implicit
   `freertos/FreeRTOS.h`, `freertos/task.h`, heap headers fails; IDF 6 requires explicit includes.

The vendored LVGL 8.3.11 itself does compile on 6.1, but this repo standardises on **LVGL 9.5 +
esp_lvgl_port 2.9.0** (the skeleton-verified path). All of the constructs above are on the
forbidden list enforced by `./cb verify` step `audit-static`.

## Vendor bug ledger (do not re-inherit)

- **QMI8658 LPF masks**: vendor writes `ctrl5 &= !QMI8658_ALPF_MASK;` — logical NOT yields 0 and
  **clears the whole register**. Correct: `&= (uint8_t)~MASK`. Our `cb_qmi8658_regs.h` exists so
  this bug class cannot reappear.
- **QMI8658 CTRL9 wait**: `while (((QMI8658_receive(STATUSINT)) & 0x80) == 0x00);` — unbounded
  loop, hangs forever on a missing/broken chip. Item 1 does not use CTRL9 host commands at all;
  any future use must add a timeout.
- **QMI8658 init order**: vendor calls `setState(sensor_running)` before ever checking WHO_AM_I,
  and its address macros disagree with the wiki (code uses 0x6B, wiki says 0x6A default) → our
  driver probes **both** addresses and logs the winner.
- **Arduino backlight duty**: vendor `Display_ST7789.cpp` computes 100 % as duty `1024` at 10-bit
  resolution (max 1023) — an off-by-one that can wrap the PWM. `cb_math_duty_from_percent()`
  clamps to `CB_MATH_BACKLIGHT_MAX 1023` and is host-tested.
- **SD auto-format**: vendor mounts with `format_if_mount_failed = true`, silently wiping cards.
  Forbidden construct here; the BSP always passes false.
- **Vendor Arduino LVGL glue**: LVGL 8 API (`lv_disp_drv_t`) and an unsafe flush cast
  `(uint16_t *)&color_p->full`. Do not port — rewrite against LVGL 9 if needed.

## LVGL 9 vs LVGL 8 (quick table for anyone porting old snippets)

| LVGL 8 (vendor/old tutorials) | LVGL 9.5 (this repo) |
|---|---|
| `lv_disp_drv_t` + `lv_disp_draw_buf_t` + `lv_disp_drv_register` | `lv_display_t` via `lv_display_create` / esp_lvgl_port `lvgl_port_add_disp` |
| `lv_scr_act()` | `lv_screen_active()` |
| `lv_color_t` 16-bit struct assumptions | `LV_COLOR_FORMAT_RGB565` + `swap_bytes` in the port config |
| `LV_USE_PERF_MONITOR` (v8 Kconfig names) | still `CONFIG_LV_USE_PERF_MONITOR` in LVGL 9.5's Kconfig (verified present) |

Both `lv_disp_drv_t` and `lv_scr_act(` are forbidden constructs (`audit-static`).

## PSRAM (octal) and DMA

- `CONFIG_SPIRAM_MODE_OCT=y` is **mandatory** (ESP32-S3R8 has octal PSRAM). Wrong/missing mode:
  boot loop or 0 bytes PSRAM at runtime *(hardware symptom; config side is asserted by
  `sdkconfig-assert`)*.
- The verified LVGL buffer recipe: double buffer 172×40 px, RGB565, `buff_dma` **and**
  `buff_spiram`, `swap_bytes=true`. Changing flags without re-testing on hardware risks DMA
  alignment/tearing issues *(hardware)*.
- `CONFIG_SPIRAM_USE_MALLOC` is already the IDF default when SPIRAM is on; it is set explicitly in
  the board defaults for clarity.
- Later Wi-Fi work should keep an eye on PSRAM contention (buffers vs radio RX).

## LCD symptoms *(hardware)*

| Symptom | Cause | Fix |
|---|---|---|
| Image shifted / 34 px missing at one edge | missing x-gap | `esp_lcd_panel_set_gap(panel, CB_LCD_X_GAP, CB_LCD_Y_GAP)` |
| Red/blue swapped | element order or byte swap | `LCD_RGB_ELEMENT_ORDER_RGB` + `swap_bytes` as in the skeleton |
| Colours inverted | missing inversion | `esp_lcd_panel_invert_color(panel, true)` |
| Backlight flashes at boot | GPIO46 is a strapping pin | unavoidable at reset; BSP starts PWM at duty 0 (item 2) |
| Nothing on screen, logs fine | panel reset/init order | follow the frozen init order in [hardware.md](hardware.md#lcd-st7789-172320) |

The `01_lcd_hello` diagnostic screen (colour bars + four edge markers, item 2) exists specifically
to make these visible at a glance on hardware day.

## Flashing and serial

- **No `/dev/ttyACM*`**: charge-only cable, missing `dialout` group, or the board needs download
  mode (hold BOOT → tap RESET → release BOOT). Details: [flashing.md](flashing.md).
- **Firmware runs but console is silent**: console not on USB-Serial/JTAG — IDF needs
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (board defaults), Arduino needs `CDCOnBoot=cdc` +
  `USBMode=hwcdc` (locked FQBN). UART0 is not broken out.
- **Early boot lines never appear** (banner / bootloader / WARN right after reset): the native USB
  peripheral re-enumerates and the host re-attaches ~1 s late — inherent to USB-Serial/JTAG
  (measured 2026-11-08). Watch for the root app's one-shot `boot summary:` line on the first
  heartbeat instead; a raw `cat /dev/ttyACM0` shows the same stream.
- **`./cb monitor` silent while `cat /dev/ttyACM0` streams**: fixed in the item-1 fix round —
  monitor is a live pass-through now; make sure stdin is a terminal (it warns otherwise, Ctrl-]
  may not reach it).
- **Port disappears mid-flash**: normal — native USB re-enumerates on reset; wait and retry.
- **Flash succeeds, app boot-loops**: suspect PSRAM config (above) or a brownout from a bright RGB
  LED at startup (item 3 caps brightness).

## Partition-table pitfalls

- `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` is resolved **relative to the project dir**. The shared
  board defaults deliberately do not name a CSV; each project's own defaults file does
  (`config/partitions.csv` from the root, `../../config/partitions.csv` from an example).
- `-DCONFIG_PARTITION_TABLE_CUSTOM_FILENAME=<path>` on the command line is **inert** (the
  Kconfig-generated value wins) — never rely on it; `./cb` asserts the generated
  `partition_table/*.bin` and its `factory … 3M` / `storage … 0x800000` rows instead.
- A `set(SDKCONFIG_DEFAULTS ...)` listing a file that does not exist is an IDF **FATAL_ERROR** at
  configure time — commit CMakeLists and its defaults file together.

## Component manager and offline builds

- Caches on this machine: `~/.espressif/tools/components/` (local mirror: lvgl 9.5.0, led_strip
  3.1.1) and `~/.cache/Espressif/ComponentManager/` (download cache incl. esp_lvgl_port 2.9.0).
  With these warm, builds are fully offline; `./cb deps` re-warms after a cache wipe (network once).
- Committed `dependencies.lock` files pin the resolved graph (lvgl 9.5.0, esp_lvgl_port 2.9.0,
  led_strip 3.1.1, idf 6.1.x). If resolution ever drifts, `verify` flags it — re-run `./cb deps`
  and let the project owner re-commit the lock.
- `IDF_COMPONENT_MANAGER=0` **disables** dependency resolution entirely (only the exact string `0`
  does this) — it is not an offline switch; `./cb verify --offline` uses `unshare -rn` instead.
- `managed_components/` directories are generated, git-ignored, and safe to delete with
  `./cb clean --deep`-style operations only inside your own `CB_BUILD_ROOT`-driven workflow.

## Shared-tree build hazards (multi-agent)

- Raw `idf.py` writes `sdkconfig` into the **project dir** — two agents raw-building the same
  project race. Rule: raw builds only by the project owner; everyone else uses `./cb build`
  (per-agent `-DSDKCONFIG` + repo `flock` at `<build_root>/.cb.lock`).
- Never delete/edit another agent's `build/<name>/` tree; use your own `CB_BUILD_ROOT`.
- Disk: each LVGL build dir is a few hundred MB — `./cb clean` when idle.

## Arduino path

- Wrong core selected (`arduino:esp32` 2.0.18 instead of `esp32:esp32` 3.3.12) → missing
  `rgbLedWrite`, old LEDC API, wrong PSRAM options. Check `arduino-cli core list`.
- `Wire.begin()` without pins uses S3 defaults (wrong here): always
  `Wire.begin(CB_PIN_I2C_SDA, CB_PIN_I2C_SCL, CB_I2C_FREQ_HZ)` — SDA (48) first.
- Deprecated/forbidden: `neopixelWrite(` (use `rgbLedWrite`), `ledcSetup(`, `ledcAttachPin(`.
- `SD_MMC.begin(...)` has a format-on-fail argument in some overloads — never pass true.
- No PSRAM in a sketch build → the FQBN lost `PSRAM=opi`; re-check the full string in
  [arduino.md](arduino.md#the-locked-fqbn).
