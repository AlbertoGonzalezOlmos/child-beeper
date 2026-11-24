# Documentation index — child-beeper programming toolkit

> Status: item-6's BSP **0.5.0**, `cb_bsp_part_result()`, exact self-test grammar and dashboard
> contract are frozen. Its full gate and serial-half capture are recorded; the final round-2 gate,
> dashboard visuals and live/alive soak are **PENDING**. Item-5 full gate is **14
> pass, 0 fail, 2 skip**. Item-2/3/4 serial evidence and item-5's no-card degraded capture are
> recorded in verification.md; mounted SD I/O remains PENDING. Levels + checklist:
> [verification.md](verification.md).

Toolkit for the **Waveshare ESP32-S3-LCD-1.47B** (ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM,
172×320 ST7789 LCD *without touch*, QMI8658 IMU, 1× RGB LED, BOOT button, battery ADC, TF card,
native USB-Serial/JTAG). It exists so later feature tasks (UI, comms, power) only add app logic on
top of a reusable BSP (`components/cb_bsp`) — on both ESP-IDF v6.1 and Arduino (core 3.3.12).

## Read in this order

| Doc | What it answers |
|---|---|
| [hardware.md](hardware.md) | Pin map, LCD x-gap 34, strapping-pin warnings, battery math, no-touch input model |
| [idf.md](idf.md) | Environment, `./cb` usage, raw `idf.py`, project pattern, partitions, offline builds |
| [api.md](api.md) | Frozen BSP API: `cb_bsp_init` + part bitmask + error policy, pins/math/FSM/QMI headers |
| [flashing.md](flashing.md) | USB-Serial/JTAG console, port guard (exit 2), BOOT+RESET recovery |
| [arduino.md](arduino.md) | arduino-cli 1.4.1, core 3.3.12, the locked FQBN, `rgbLedWrite`, library staging |
| [verification.md](verification.md) | What `./cb verify` proves, build- vs hardware-verified, **hardware-day checklist** |
| [troubleshooting.md](troubleshooting.md) | Vendor demo's measured IDF-6.1 breakage, vendor bug ledger, PSRAM/DMA, LVGL 9 vs 8, offline notes |

Also: [`README.md`](../README.md) (30-second quickstart) and [`AGENTS.md`](../AGENTS.md) (rules for
agents working in this tree: one writer per file, frozen names, commit format).

## Quickstart (board optional)

```bash
. ~/.espressif/tools/activate_idf_v6.1.sh   # or let ./cb locate it for you
export CB_BUILD_ROOT=build/<your-name>      # only when sharing the tree with other agents
./cb doctor                                 # toolchain green?
./cb deps                                   # once: warm component cache, write locks
./cb verify                                 # the full no-board gate; ./cb report to re-read
```

With a board attached (first session done 2026-11-08 — checklist in
[verification.md](verification.md)):

```bash
./cb ports                     # verified 2026-11-08: exactly /dev/ttyACM0
./cb flash --target app        # hardware-verified; exit 2 + BOOT/RESET hint only when no port
./cb monitor                   # streams live; Ctrl-] quits
```

## Repository layout (item 1)

```
cb                            # bash shim → tools/cb.py (Python 3 stdlib CLI)
CMakeLists.txt  main/         # root IDF app ("hello beeper")
components/cb_bsp/            # the BSP: CMakeLists, include/ (frozen headers), src/
                              #   item 2: cb_lcd/cb_backlight/cb_pattern
                              #   item 3: cb_rgb/cb_button/cb_battery + pure math
                              #   item 4: cb_i2c/cb_imu + pure math
                              #   item 5: cb_sd
                              #   item 6: cb_bsp_part_result + version 0.5.0
config/                       # sdkconfig.board.defaults + partitions.csv (3M factory, 8M FAT)
examples/01_lcd_hello/        # canonical example pattern; item 2: display diagnostic app
examples/02_rgb_button/       # item 3: capped RGB + BOOT-button events
examples/03_battery/          # item 3: calibrated/fallback battery samples
examples/04_imu/              # item 4: QMI8658 IMU diagnostic
examples/05_sdcard/           # item 5: exclusive-create TF-card round-trip
examples/06_dashboard/        # item 6: seven-part self-test + live LVGL dashboard
tests/host/                   # host-compiled unit tests (pins, math, button FSM)
tools/                        # cb.py + cbchecks/ (verify pipeline, static audits)
docs/                         # this directory
arduino/                      # item 7: staged CBBoard library + sketches (absent in item 1)
```

## Feature status matrix

| Feature | BSP module | Example | Build-verified | Hardware-verified |
|---|---|---|---|---|
| Lifecycle/version | `cb_bsp_init` | root app | item 1 | ✓ boot + console + 11 min soak (2026-11-08) |
| Pins/math/FSM/QMI regs (pure, host-tested) | `cb_pins.h`, `cb_math`, `cb_button_fsm`, `cb_qmi8658_regs` | `tests/host` | item 1 | n/a (logic) |
| LCD + LVGL 9.5 + backlight + pattern | `cb_lcd`, `cb_backlight`, `cb_pattern` | `01_lcd_hello` diagnostic | item 2 (2026-11-08) | ✓ serial 2026-11-23 (init, backlight hook, 12 frames); visuals PENDING |
| RGB LED, BOOT button, battery | `cb_rgb`, `cb_button`, `cb_battery` | `02_rgb_button`, `03_battery` | **item 3 build-verified** (round-2 `d7fabd3`: 12 pass, 0 fail, 2 skip, 129.0 s, 2026-11-23) | serial-verified: battery sample, RGB 20 % cap, button poller, boot summary 0.2.0; visual/multimeter/gesture PENDING (needs human) |
| IMU QMI8658 (new `i2c_master`) | `cb_i2c`, `cb_imu`, `cb_imu_math` | `04_imu` | **item 4 build-verified**: 13 pass, 0 fail, 2 skip; sdkconfig-assert 5 targets | ✓ serial 2026-11-23: 0x6B, WHO 0x05, stable 0.99 g IMU samples; motion response + accuracy/bias calibration PENDING (needs human) |
| TF card (SDMMC, no auto-format) | `cb_sd` | `05_sdcard` | **item 5 build-verified**: 14 pass, 0 fail, 2 skip; sdkconfig-assert 6 targets | ✓ no-card degraded path 2026-11-23: one direct init, both widths timed out, alive continued; mounted I/O and successful 1-bit fallback PENDING. A mount error is not proof that no card exists. |
| Self-test + dashboard | `cb_bsp_part_result`, BSP **0.5.0** | `06_dashboard`, `./cb selftest` | **Full gate 15/0/2 recorded**; app + six examples, sdkconfig 7 targets; current 47 host + **170 CLI** tests after `acd83ef`; final round-2 gate PENDING | **✓ serial-half 2026-11-23** — exit 0, exact gold shapes, battery 4209 mV, IMU 0x6B/1.00 g, 2 SKIPs unverified; visuals, live/alive soak, mounted card and root 0.5.0 PENDING |
| Arduino parity (CBBoard, sketches) | item 7 | `arduino/` | item 7 | ✗ |
| Wi-Fi / BLE scan (tier-2) | — | `08_wifi_scan`, `09_ble_scan` | item 8 | ✗ |

Update this table in the review round of each item — it is the one place that says, at a glance,
what exists and what is proven.
