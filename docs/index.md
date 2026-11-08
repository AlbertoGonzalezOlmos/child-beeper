# Documentation index — child-beeper programming toolkit

> Status: item 1 (updated 2026-11-08, board attached). The toolkit is build-verified; flash,
> console and an ~11-minute app soak are **hardware-verified** — everything else is still
> build-verified only. Levels + checklist: [verification.md](verification.md).

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
                              #   item 2 adds cb_lcd, cb_backlight, cb_pattern
config/                       # sdkconfig.board.defaults + partitions.csv (3M factory, 8M FAT)
examples/01_lcd_hello/        # canonical example pattern; item 2: display diagnostic app
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
| LCD + LVGL 9.5 + backlight + pattern | `cb_lcd`, `cb_backlight`, `cb_pattern` | `01_lcd_hello` diagnostic | item 2 (2026-11-08) | ✗ pending orchestrator flash |
| RGB LED, BOOT button, battery | item 3 | `02_rgb_button`, `03_battery` | item 3 | ✗ |
| IMU QMI8658 (new `i2c_master`) | item 4 | `04_imu` | item 4 | ✗ |
| TF card (SDMMC, no auto-format) | item 5 | `05_sdcard` | item 5 | ✗ |
| Self-test + dashboard | item 6 | `06_dashboard`, `./cb selftest` | item 6 | ✗ |
| Arduino parity (CBBoard, sketches) | item 7 | `arduino/` | item 7 | ✗ |
| Wi-Fi / BLE scan (tier-2) | — | `08_wifi_scan`, `09_ble_scan` | item 8 | ✗ |

Update this table in the review round of each item — it is the one place that says, at a glance,
what exists and what is proven.
