# Verification — what is proven, how, and what still needs the board

> Status (updated for item 4): item 3 is **build-verified** (12 pass, 0 fail, 2 skip), with
> item-2 display and item-3 `cb_bsp=0.2.0`, RGB cap, button-poller and calibrated-battery serial
> checks **hardware-verified**. Item-4's full coordinator gate and all I2C/IMU serial checks are
> **PENDING**; no item-4 hardware fact is claimed. Display/LED visuals, button gestures, multimeter
> accuracy and IMU motion/accuracy checks remain PENDING (needs human). Details and dates follow.

This repo distinguishes two levels of proof, and every doc/report must say which one applies:

| Level | Meaning | How it is established |
|---|---|---|
| **build-verified** | Compiles and links cleanly on this machine with the pinned toolchain; static checks pass | `./cb verify` (below) |
| **hardware-verified** | Observed working on the physical board | Only by running the hardware-day checklist below and recording results |

Rule (also in `AGENTS.md`): *never claim hardware-verified without a board.*

## Hardware status (measured 2026-11-08, board on `/dev/ttyACM0`)

**hardware-VERIFIED**
- Chip identity (`esptool flash_id`): **ESP32-S3 QFN56 rev v0.2**, features *Embedded PSRAM 8MB
  (AP_3v3)*, *Detected flash size: 16MB* — the OCT-PSRAM/16MB-flash config matches silicon.
- `./cb flash --target app` flashes successfully; the app boots and runs.
- USB-Serial/JTAG console works: a raw `cat /dev/ttyACM0` streams
  `I (22519) hello_beeper: alive #12` every 2 s.
- Stability: the app ran **~11 minutes** (`alive #323`) with no WDT reset / reboot loop.
- `./cb ports --json` returns exactly `/dev/ttyACM0`; `./cb monitor` streams the console live
  (`Ctrl-]` quits).
- Guard logic with a real board present: `CB_FORCE_NO_PORTS=1 ./cb flash` still exits **2**
  (step `guards-no-board`).
- **Item-2 display session (2026-11-08)**: `./cb flash --target 01_lcd_hello` succeeds; serial
  shows `I (701) cb_lcd: display: 172x320 ST7789, spi2 @40MHz, gap(34,0)` and
  `I (732) lcd_hello: display: 172x320 ST7789, spi2 @40MHz, gap(34,0), lvgl 9.5`, then
  `lcd_hello: frame 1..6` every 2 s — panel init, LVGL port and the render loop run on hardware.
  (The first flash attempt boot-looped on the `esp_lvgl_port_disp.c:120` io_handle assert;
  fixed in commit 92e336d together with the internal-DMA buffer config.) **Colours, x-gap,
  inversion, mirroring and tearing remain hardware-PENDING** — they need a
  human looking at the panel (checklist item 6).
- **Item-2 boot/display serial session (2026-11-23)**: the root app printed
  `boot summary: reset=USB cb_bsp=0.1.0 init(CB_PART_ALL)=ESP_OK idf=v6.1 heap int=206355B/169924B psram=8269144B`,
  satisfying checklist 3 and recording the pre-item-3 heap baseline. `01_lcd_hello` printed the exact
  A5 display line, `cb_lcd: first frame done: backlight 100%`, and `lcd_hello: frame 1` through
  `frame 12` at approximately 2 s intervals with no reset/WDT. The serial half of checklist 6 and
  the first-frame backlight hook are therefore **hardware-verified**; every visual check remains
  **hardware-PENDING (needs human)**.
- **Attached-board fast gate (2026-11-23)**: `CB_BUILD_ROOT=build/coordinator ./cb verify --fast`
  completed with **7 pass, 0 fail, 5 skip**, including 106 CLI tests.
- **Item-3 full coordinator gates (2026-11-23)**: round 1 at `c6447a6` completed
  **12 pass, 0 fail, 2 skip** in 707.7 s. The round-2 gate at **`d7fabd3`** also completed
  **12 pass, 0 fail, 2 skip**, in **129.0 s**. App and all three examples built; sdkconfig-assert
  passed all four targets; 109 CLI tests passed; the two SKIPs are the still-absent Arduino tree and
  sketches. **Round-3 final full-gate result: PENDING coordinator report after the round-3 fixes.**
- **Item-3 serial session (2026-11-23)**: the root app printed
  `boot summary: reset=USB cb_bsp=0.2.0 init(CB_PART_ALL)=ESP_OK idf=v6.1 heap int=199763B/163332B psram=8269032B`
  and continued through `alive #7`; only I2C/IMU/SD still log `not implemented`. `02_rgb_button`
  printed the exact init/poller/ready lines and all five capped cycle values, continuing through
  `cycle 9` and `alive #3`. `03_battery` printed `cb_battery: adc up, cali curve` and stable
  `calibrated=1 plausible=1` samples. Exact quoted lines are in checklist 7 below. LED visuals,
  button gestures, multimeter accuracy and physical cell presence remain **PENDING (needs human)**.
- **Item-4 gate/serial status**: item-4's required target, pure decode tests and checklist-8
  acceptance contract are defined below. Its full coordinator gate, `cb_bsp=0.3.0` root summary and
  04_imu hardware capture are **PENDING**; no item-4 hardware fact is claimed yet.
- **Item-3 round-1 agent-side checks (2026-11-23; supplementary, not the full gate)**: with
  `CB_BUILD_ROOT=build/kimi-k31-pro`, `./cb test` passed all **39** host tests (`# failed 0`),
  `./cb verify --fast` completed **7 pass, 0 fail, 7 skip** (109 CLI tests), and
  `./cb build app 02_rgb_button 03_battery` passed all three targeted builds.
- **Item-3 round-2 agent-side checks (2026-11-23; after the R13b/R14b/R11b fixes)**: `./cb test`
  passed all **38** host tests (`# failed 0`; the IDF-header button-poll test and stub were removed),
  `python3 -m unittest discover -s tools/tests -t . -q` passed **109** tests, `./cb verify --fast`
  completed **7 pass, 0 fail, 7 skip**, and `./cb build app 02_rgb_button 03_battery` passed all
  three targets. The completed round-2 coordinator result is the `d7fabd3` gate above.

**hardware-UNAVAILABLE (with reasons)**
- **Early boot lines may be unobservable** on the USB-Serial/JTAG console: after a reset the native
  USB peripheral re-enumerates and the host may re-attach only after the app's early output. Whether
  a particular banner/WARN line is captured depends on that timing. On 2026-11-23 the monitor did
  capture the `I (683)` banner and `W (684…)` part warnings, so categorical loss is disproven. The
  root app's **one-shot boot summary on its first heartbeat**
  (`boot summary: reset=… cb_bsp=… init(CB_PART_ALL)=… idf=…`) remains the reliable observation
  point.
- **BOOT+RESET recovery drill** (hold BOOT → tap RESET → release) needs a physical finger on the
  buttons — still pending.
- **Wi-Fi STA connect** (item 8): the root app reads `wifi_settings.txt` from the FAT `storage`
  partition (embedded at build time, flashed with `./flash.sh`) and connects with `esp_wifi`.
  **Build-verified only** — the image round-trip (`fatfsparse.py` shows the settings inside
  `storage.bin`) and the whole pipeline are proven on the host; the actual connection, DHCP
  answer, retry path and the `wifi=…` heartbeat states still need the board on a real network.

## `./cb verify` — the no-board gate

Run from the repo root:

```bash
export CB_BUILD_ROOT=build/<your-agent-name>   # parallel-agent convention
./cb verify            # full gate (builds everything)
./cb verify --fast     # doctor + host tests + static audits + guards only (no builds)
./cb verify --offline  # additionally proves the build works without network (see below)
```

Steps run in a **fixed order** with **stable ids**; each prints `[PASS]/[FAIL]/[SKIP] <id> <detail>`:

1. `doctor` — toolchain/environment checks (IDF v6.1, xtensa gcc 15.2.0, arduino-cli 1.4.1, core
   `esp32:esp32` 3.3.12, disk space, serial ports). **No board is a WARN here, never a FAIL.**
2. `host-tests` — `make -C tests/host test`; binary at `${CB_BUILD_ROOT:-build}/host/run_tests`;
   TAP-ish output (`ok - <name>` / `not ok - <name>` / final `1..N`). Covers the pin-macro contract,
   `cb_math` (duty clamp, LE decode, scale math, ADC conversion), `cb_button_fsm` (single click,
   double click, long press, bounce, injected time) and — from item 2 — `cb_pattern`
   (exact-pixel palette, bar boundaries, ramp endpoints, 2×2 corner markers, stride/OOB guards).
   Item 3 adds `cb_rgb_math` and `cb_battery_math` plus these exact test groups:
   `test_rgb_dim_cap_macro` (20%), `test_rgb_math_dim` (NULL, low values unchanged, peak cap,
   truncation and idempotence), `test_battery_macro_contract` (divider/gain and 2500/4500 mV bounds),
   `test_battery_math_conversion` (negative→0, 695→2100, 1000→3021, **1333→4027**, 4095→12373
   and INT32_MAX saturation), and `test_battery_math_plausible` (2499 false, inclusive 2500/4500,
   4501 false). The R11b amendment removed the IDF `esp_err.h` stub and the button-poll macro's
   IDF-header test; constants are asserted through pure headers only. No IDF peripheral driver
   source is host-compiled.
   Item 4 adds `cb_imu_math` and nine tests (38 + 9 = **47** total): `test_imu_math_raw_len`
   (CB_IMU_RAW_LEN==14), `test_imu_math_decode_resting` (temp 25 °C and AZ=1 g at 8 g),
   `test_imu_math_decode_accel_sign` (-8192→-2 g, 2048→0.5 g, -4096→-1 g at 8 g),
   `test_imu_math_decode_gyro` (8192→128, -16384→-256, 256→4 dps at 512 dps),
   `test_imu_math_decode_ranges` (4096 raw→0.25/0.5/1/2 g at 2/4/8/16 g; gyro MIN→-16 dps;
   MAX at 1024→1023.96875 dps), `test_imu_math_decode_temp` (0/1/25/0.5/-1/-128 °C),
   `test_imu_math_decode_layout` (TEMP,AX,AY,AZ,GX,GY,GZ at offsets 0,2,4,6,8,10,12),
   `test_imu_math_decode_invalid` (NULL/invalid ranges return false with out byte-identical), and
   `test_imu_math_decode_int16_min` (0x8000 decodes to exact negative range without signed
   negation/UB). Existing helper tests also cover temp 0xFF80→-0.5 and full-scale
   +32767→range*32767/32768 rather than +range.
3. `cli-tests` — the CLI's own unit tests:
   `python3 -m unittest discover -s tools/tests -t . -q` (a missing `tools/tests/` is a FAIL,
   owner deepseek-flash). The current item-4 suite has **112 tests**, including the added required
   target and five-lock fixtures.
4. `pin-drift` — byte-compares Arduino copies of the shared headers against
   `components/cb_bsp/include/`. **SKIP while `arduino/` is absent.**
5. `audit-static` — forbidden constructs + absolute-path scan (scope below).
6. `deps-pins` — every committed `dependencies.lock` must resolve **lvgl 9.5.0**,
   **esp_lvgl_port 2.9.0**, **led_strip 3.1.1**, **idf 6.1.0**, and
   `components/cb_bsp/idf_component.yml` must pin those three exactly (no `^`/`~`/`>=` ranges).
   A missing/stale lock FAILs with a `./cb deps` hint — run `./cb deps` once after any
   manifest/lock change (ruling A9). Item 4 has five project locks: root, `01_lcd_hello`,
   `02_rgb_button`, `03_battery`, and `04_imu`. Each example lock is committed by the example owner
   after its first successful build; dependency versions themselves do not change.
7. `build:app` — builds the root app (`main/` "hello beeper").
8. `build:<example>` — one step per `examples/*`. Item 3 adds required steps
   `build:02_rgb_button` and `build:03_battery` after `build:01_lcd_hello`; item 4 adds the required
   `build:04_imu` step.
9. `sdkconfig-assert` — per built target: OCT PSRAM, PSRAM 80 M, 16 MB flash, USB-Serial/JTAG
   console, custom partition table; plus the built partition binary exists and the table contains
   `factory,app,factory,0x10000,3M` and `storage,data,fat,0x310000,0x800000`.
   **For `app` and `01_lcd_hello` it additionally requires** (A9; key list quoted from
   `tools/cbchecks/build.py:SDKCONFIG_LVGL_KEYS`): `CONFIG_LV_CONF_SKIP=y`,
   `CONFIG_LV_COLOR_DEPTH_16=y`, `CONFIG_LV_COLOR_DEPTH=16`, `CONFIG_LV_OS_NONE=y`,
   `CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y`, `CONFIG_LV_FONT_MONTSERRAT_14=y`,
   `CONFIG_LV_USE_LABEL=y`, `CONFIG_LV_USE_IMAGE=y`; and **forbids** `CONFIG_LV_CONF_MINIMAL=y`.
   Absence of any `CONFIG_LV_` config in those two targets is a **FAIL, not a skip**. For every
   other target, including 02/03/04, the same eight keys are checked **whenever any `CONFIG_LV_*`
   entry is present**; cb_bsp's manifest normally makes them present even though those examples
   do not call LVGL. (`CONFIG_LV_OS_NONE=y` is deliberate: esp_lvgl_port owns the LVGL task;
   demanding an `LV_OS_FREERTOS`-style setting would fail a correct build.)
10. `arduino:<sketch>` — while `arduino/` is absent a single **synthetic step id `arduino:*`** is
    emitted as SKIP instead of per-sketch steps.
11. `guards-no-board` — runs `./cb flash` / `./cb monitor` with `CB_FORCE_NO_PORTS=1`; both must
    exit **2** with the BOOT+RESET recovery hint, without invoking any build or esptool.
12. `report` — writes `${CB_BUILD_ROOT:-build}/verify-report.md` and `.json` (per-step status,
    durations, app sizes via `idf.py size --format json` when it succeeds, `.bin`-size fallback
    otherwise). `./cb report` prints the markdown.

**Item-1 strictness + item-3 R9b + item-4 R25:** `tests/host/Makefile`, `build:app`,
`build:01_lcd_hello`, `build:02_rgb_button`, `build:03_battery`, and `build:04_imu` are *required*
— missing files make those steps **FAIL**, not SKIP, even in `--fast` mode. The full and fast
missing-04 tests both require detail `examples/04_imu missing (CMakeLists.txt not found)`. SKIP is
legitimate only for `pin-drift`, `arduino:*` (no `arduino/` tree yet), build and dependent
`sdkconfig-assert` steps deliberately skipped by `--fast`, and `--offline` sub-checks when
`unshare` is unavailable. With all item-4 projects
present, a normal full gate still has the two expected Arduino-related SKIPs; sdkconfig-assert
covers **five** targets (app + four examples).

**Exit codes:** a FAIL in `doctor` → **3**; any other FAIL → **1**; all PASS/SKIP → **0**.
(Globally: 0 ok, 1 build/test failure, 2 board/port absent, 3 usage/environment.)

**`--fast`** skips every present build step (same ids, marked SKIP) — useful while other slices
are landing. A missing required project is still a FAIL under the strictness rule above. During
shared item-3 implementation, slice agents use this fast gate and targeted builds only; the
coordinator owns the final full gate.

**`--offline`** re-runs the builds inside `unshare -rn` (network-less namespace) where available and
forbids fetch commands; if `unshare` is unavailable the build steps report SKIP with explicit
wording ("offline namespace unavailable — NOT proven offline") **and `verify --offline` exits 1**
("gate NOT fully proven") — unless `--fast` was given, which requests no builds anyway. It never
uses `IDF_COMPONENT_MANAGER=0` (that disables managed dependencies entirely and would make builds
fail for the wrong reason).
Offline builds work because the dependency pins (lvgl 9.5.0, esp_lvgl_port 2.9.0, led_strip 3.1.1)
are all present in the local caches — see [troubleshooting.md](troubleshooting.md#component-manager-and-offline-builds).

## `audit-static` scope (what it scans)

- **Construct rules** (forbidden list below) scan only `.c .h .cpp .hpp .ino CMakeLists.txt .cmake`
  files under `main/`, `components/`, `examples/`, `tests/` and `tools/` (the whole `tools/` tree).
  `docs/**` and `*.md` are **exempt** — documentation must be able to quote forbidden symbols
  (this file and [troubleshooting.md](troubleshooting.md) do exactly that).
- **Absolute-path rule**: no absolute home-directory paths in any tracked text file (docs use
  `~/…` form); single allowlist entry `eim_config.toml` (pre-existing, untouchable).
- Forbidden constructs (live in `tools/cbchecks/`): `driver/i2c.h`, `lcd_color_rgb_endian_t`,
  `LCD_RGB_ENDIAN_`, `lv_disp_drv_t`, `lv_disp_draw_buf_t`, `lv_scr_act(`, `ledcSetup(`,
  `ledcAttachPin(`, `neopixelWrite(`, `format_if_mount_failed = true`, and `ESP_ERROR_CHECK(`
  inside `cb_bsp` on optional hardware.

## What `./cb verify` does **not** prove

- Any runtime behaviour beyond the app heartbeat: display output, colours, x-gap correctness, IMU
  data, battery accuracy, SD I/O, button feel, RGB brightness, radio behaviour — checklist below.
- That frozen peripheral constants match silicon (ADC unit / SPI host enum values are
  contract-checked by host tests; the electrical truth is a hardware-day item). Chip identity,
  flash size and PSRAM presence **are** now hardware-verified (above).
- Early-boot log content (it may be dropped by USB re-enumeration — use the reliable one-shot
  boot-summary line; the 2026-11-23 session did capture early lines).
- Code-level display facts: backlight duty-0-at-init, the `LV_EVENT_REFR_READY` bring-up hook,
  `cb_bsp_init` idempotency, LVGL port defaults — proven by code review + compiling, not by gate
  steps (the gate deliberately does not fake these with source greps).
- Anything under `arduino/` until item 7 lands.

`./cb monitor` itself is a live pass-through: it prints `cb: monitoring — press Ctrl-] to quit`,
streams the console, warns when stdin is not a terminal (Ctrl-] may not reach it), and honours
`CB_MONITOR_TIMEOUT` (default 86400 s).

## Hardware-day checklist (run when a board is attached; record results here)

Prerequisites: USB-C **data** cable; user in `dialout`; see [flashing.md](flashing.md).
Procedure per line: run the command, compare with "expected", tick and fill in date/result.

- [x] 1. Board enumerates: `./cb ports --json` returns exactly `/dev/ttyACM0` —
      **verified 2026-11-08**.
- [x] 2. `./cb flash --target app` succeeds; the console streams `hello_beeper: alive #N` every 2 s
      (`./cb monitor` live, `Ctrl-]` quits; or raw `cat /dev/ttyACM0`); ~11 min soak
      (`alive #323`) with no WDT/reset — **verified 2026-11-08**. Early banner/WARN lines may be
      lost depending on USB re-attachment timing; the 2026-11-23 session captured them — see
      "Hardware status" above.
- [x] 3. **Boot summary**: after a fresh flash/reset, the first heartbeat carries the one-shot
      `boot summary: reset=… cb_bsp=0.1.0 init(CB_PART_ALL)=… idf=…` line. **Verified
      2026-11-23**: `reset=USB cb_bsp=0.1.0 init(CB_PART_ALL)=ESP_OK idf=v6.1`, with
      `heap int=206355B/169924B psram=8269144B`. This matches A6 (strict LCD/LVGL parts
      implemented; six optional parts still WARN `not implemented`). Item 3 must be re-observed
      with `cb_bsp=0.2.0`; the 2026-11-23 0.1.0 record is historical evidence, not item-3 proof.
      **Item-3 root PASS, also 2026-11-23**: the current branch printed
      `boot summary: reset=USB cb_bsp=0.2.0 init(CB_PART_ALL)=ESP_OK idf=v6.1 heap int=199763B/163332B psram=8269032B`,
      then `alive #2` through `alive #7` with no reset/WDT. RGB/BUTTON/BATTERY `not implemented`
      warnings are gone; only I2C/IMU/SD remain.
- [x] 4. Guard logic with the board attached: `CB_FORCE_NO_PORTS=1 ./cb flash` → exit **2** +
      BOOT/RESET hint — **verified** via `./cb verify` step `guards-no-board`.
- [ ] 5. Recovery: hold **BOOT**, tap **RESET**, release **BOOT** → download mode; flash succeeds
      (**pending — needs a physical press**).
- [ ] 6. (item 2) **Display diagnostic** — `./cb flash --target 01_lcd_hello`, then check:
      - [x] **Serial half — PASS 2026-11-23**: exact
        `display: 172x320 ST7789, spi2 @40MHz, gap(34,0), lvgl 9.5` line, followed by
        `lcd_hello: frame 1` through `frame 12` every ~2 s with no reset/WDT. The log also showed
        `cb_lcd: first frame done: backlight 100%`, proving the A2 first-frame hook. The separate
        `cb_lcd: display: 172x320 ST7789, spi2 @40MHz, gap(34,0)` and LVGL internal-DMA lines were
        captured.
      - [ ] 8 vertical bars, left→right: **white, yellow, cyan, green, magenta, red, blue, black**
        (red↔blue swap ⇒ element order wrong; garbled hues ⇒ byte-swap wrong) — **PENDING, needs
        human eyes**;
      - four **2×2 corner markers**: TL red, TR yellow, BL cyan, BR magenta — all four visible at
        the very corners (any missing/shifted ⇒ x-gap 34 or mirroring wrong; A4 layout);
      - bottom **grey ramp** runs black→white left→right over the last `height/16` rows;
      - white bar paper-white, black bar truly black (inverted ⇒ `invert_color` wrong);
      - resolution label legible near the bottom centre, not covering the corner markers;
      - **no white flash at boot** — backlight stays dark until the first LVGL frame, then comes
        on at 100 % (A2 bring-up hook);
      - sustained frames show no tearing/corruption (internal-DMA draw buffers + PSRAM canvas;
        flags `buff_dma=true`/`buff_spiram=false`, see [api.md](api.md)).
- [ ] 7. (item 3) **RGB, BOOT button and battery** — serial evidence is ticked only for the exact
      observations quoted below; visuals, gestures and measurements remain separate PENDING items.
      - [x] **RGB init + five-cycle serial — PASS 2026-11-23**: observed
        `I (587) cb_rgb: set r=0 g=0 b=0`, then
        `I (587) cb_rgb: rmt up gpio38, cap 20%` and
        `I (588) rgb_button: ready poll_ms=10 cap_percent=20`. The full capture contains
        `rgb_button: cycle 1 r=51 g=0 b=0`,
        `rgb_button: cycle 2 r=0 g=51 b=0`,
        `rgb_button: cycle 3 r=0 g=0 b=51`,
        `rgb_button: cycle 4 r=51 g=51 b=51`, and
        `rgb_button: cycle 5 r=0 g=0 b=0`, followed by `alive #2`, `alive #3` and cycles 6–9.
        This proves the RMT transactions and the numeric 20% cap (255→51), not visible light.
        `W rgb_button: cycle %u failed: %s` was not observed; after R14b it is emitted only on the
        first failure of a streak, with the gate cleared by a successful cycle.
      - [ ] **RGB appearance (human)** — visible but dim-capped, expected GRB colours and no
        brownout. **PENDING (needs human eyes)**; no uncapped diagnostic bypass exists.
      - [x] **Button poller serial — PASS 2026-11-23**: observed
        `I (588) cb_button: poller up, 10 ms`. This proves the poller started, not gesture accuracy.
      - [ ] **Button gestures (human)** — `rgb_button: event PRESS`, `RELEASE`, `CLICK`,
        `DOUBLE_CLICK`, and `LONG_PRESS` are the frozen event strings if a human presses BOOT.
        CLICK arrives about 350 ms after release, a double yields RELEASE then DOUBLE_CLICK, and
        hold ≥800 ms yields LONG_PRESS once with no click on release. **PENDING (needs human)**;
        callback registration returning `ESP_OK` is not a poller-health check.
      - [x] **Battery samples serial — PASS 2026-11-23**: observed
        `I (587) cb_battery: adc up, cali curve`,
        `I (587) battery: ready`,
        `I (587) battery: raw=1638 pin_mv=1397 battery_mv=4221 calibrated=1 plausible=1`,
        `I (2587) battery: raw=1637 pin_mv=1396 battery_mv=4218 calibrated=1 plausible=1`, and
        `I (6587) battery: alive #1`. Curve-fitting calibration was used and the reading was stable
        (±3 mV over 10 s) with no spurious implausible WARN. The driver emits no per-read data
        line. On transition to implausible its contract is
        `W cb_battery: implausible %ld mV (no cell inserted?)`; recovery is
        `I cb_battery: reading plausible again`, both transition-gated. R13b adds one-shot
        `W battery: read failed: %s`, after which polling stops while `battery: alive #%u`
        continues every 5 s; that failure path was not injected and remains build/code-verified.
      - [ ] **Battery accuracy + cell presence (human + meter)** — compare with a multimeter
        within ±5 % and record both numbers. **PENDING**; a plausible serial sample or successful
        calibration is not accuracy evidence. 4221 mV is consistent with a charged cell or USB
        feed, so physical cell presence also remains undetermined.
      - [x] **Root R8/regression serial — PASS 2026-11-23**: observed
        `boot summary: reset=USB cb_bsp=0.2.0 init(CB_PART_ALL)=ESP_OK idf=v6.1 heap int=199763B/163332B psram=8269032B`
        and continuing `hello_beeper: alive #N` through `alive #7` with no WDT/brownout. Root
        remains unchanged: no `bat=`/`rgb=` heartbeat keys and no root button handler. Only
        I2C/IMU/SD still log `not implemented`. This boot-summary check does not by itself prove
        optional RGB/button/battery health; their separate serial lines above are the evidence.
- [ ] 8. (item 4) **I2C/QMI8658 IMU — PENDING: the coordinator has not yet flashed `app`/
      `04_imu` or supplied a dated serial capture. No item-4 hardware result is claimed.**
      - [ ] **Bus/probe/init serial** — record `i2c: internal pull-ups enabled`, the probe result
        at **0x6A** (then **0x6B** only if 0x6A does not answer), and the successful
        `cb_imu: up addr=0x%02X who=0x%02X range=8g/512dps odr=120Hz` line. Record the winning
        address and actual WHO byte verbatim; expected WHO is **0x05**. A mismatch is a WARN plus
        `ESP_ERR_INVALID_RESPONSE`, not a pass. The pull-up log proves the setting, not electrical
        suitability.
      - [ ] **Ready/sample/alive serial** — require once
        `I imu: ready addr=0x%02X who=0x%02X`, then the full
        `I imu: ax=%+.3f ay=%+.3f az=%+.3f gx=%+.2f gy=%+.2f gz=%+.2f temp_c=%+.2f`
        sample every **2 s**.
        Require `I imu: alive #%u` first at **+5 s**, then every **5 s**, including after polling
        stops. Quote the captured lines rather than treating these format strings as evidence.
      - [ ] **R23 resting acceptance** — with the board resting flat, the largest of
        `|ax|`, `|ay|`, `|az|` must be **0.9–1.1 g**; each of the other two accel axes must be
        **<0.15 g**; every `|gx|`, `|gy|`, `|gz|` must be **<2.00 dps**; and `temp_c` must be
        **10–50 °C**. Record actual numeric samples. These are sanity thresholds, not calibration,
        full-scale accuracy or axis-orientation proof.
      - [ ] **Degraded paths** — if neither address ACKs, record one
        `W imu: no IMU found (probed 0x6A/0x6B), samples skipped` and continuing alive lines with
        no ready/sample lines; mark this **SKIP/degraded, never PASS**. On a read error, require
        one `W imu: read failed: %s`, stopped polling and continuing alive lines. An unobserved
        failure path remains code/build-only.
      - [ ] **Root regression serial** — fresh `app` boot must contain
        `cb_bsp=0.3.0 init(CB_PART_ALL)=ESP_OK` and continuing `hello_beeper: alive #N`; only SD
        should remain `not implemented`. This does not prove optional IMU health.
      - [ ] **Human/electrical checks** — deliberate motion response, full-scale accuracy, axis
        orientation and internal-pull-up signal integrity at 400 kHz remain **PENDING** even after
        the serial checks above pass.
- [ ] 9. (item 5+) SD: mount logs the bus width used (4-bit, or 1-bit fallback); write/read
      round-trip passes; **no card is ever auto-formatted**.
- [ ] 10. (item 6+) `./cb selftest` → `SELFTEST`/`ITEM:`/`RESULT:` lines, `RESULT` all PASS
      (serial contract frozen in item 6).
- [ ] 11. (item 7+) One Arduino sketch flashed via `./cb arduino` behaves like its IDF twin;
      `Serial` output visible over Type-C (CDC on boot).
- [ ] 12. (item 8+) Wi-Fi scan lists APs; BLE scan lists advertisers.

Unticked boxes are **not hardware-verified**; build/code status is stated above, and human-only
checks stay pending even after a successful build. Ticked boxes record measured facts with dates.
