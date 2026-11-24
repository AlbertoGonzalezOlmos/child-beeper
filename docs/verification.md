# Verification — what is proven, how, and what still needs the board

> Status (updated for item 6): item-4 full gate is **13 pass, 0 fail, 2 skip**, with item-2/3/4
> serial checks **hardware-verified** as detailed below. Item-5's full gate is **14 pass, 0 fail,
> 2 skip** and its no-card degraded path is **hardware-verified 2026-11-23**. Item-7's full gate is
> **18 pass, 0 fail, 0 SKIP**, with real pin-drift and both required Arduino builds; `rgb_blink`
> also has a measured serial-half PASS. `button_events` upload and visible LED/gesture behavior
> remain **PENDING**. Item-6's **15 pass, 0 fail, 2 skip** full gate and serial-half self-test capture are
> recorded. Its final round-2 full gate, root 0.5.0 regression, live/alive soak and dashboard
> visual checks are **PENDING**. Earlier root-version and mounted SD captures retain their
> historical status. Display/LED visuals, button gestures, multimeter accuracy, IMU
> motion/accuracy/bias calibration and mounted SD I/O remain PENDING (needs human/card). Details
> and measured dates follow; no item-6 example below is itself hardware evidence.

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
- **Item-4 gate/serial status**: the full gate on the item-4 tip completed **13 pass, 0 fail,
  2 skip**; app and all four examples built, sdkconfig-assert passed all **five** targets, host
  tests passed **47**, and CLI tests passed **112**. The `cb_bsp=0.3.0` root summary remains
  **PENDING**. The 2026-11-23 `04_imu` capture is a **serial-half PASS**: 0x6B answered after 0x6A
  was silent, WHO_AM_I was 0x05, revision was 0x7C, and stable decoded samples gave 0.99 g magnitude
  with approximately 31 °C temperature. Hardware also resolved ADDR_AI as **0x40** and established
  that CTRL9 **0x12** is required; both fixes are in `a05fded`. Exact lines and the still-pending
  motion/accuracy/bias checks are in checklist 8 below.
- **Item-5 gate/card status**: the full gate on `f8b83c4` completed **14 pass, 0 fail, 2 skip**;
  app and all five examples built, sdkconfig-assert passed all **six** targets, host tests passed
  **47**, and CLI tests passed **115**. The root `cb_bsp=0.4.0` capture remains **PENDING**. The
  2026-11-23 `05_sdcard` capture shows one direct init, timeout on both widths, and a continuing
  heartbeat: the no-card degraded path is **hardware-verified**. **A mount error is not proof that
  no card exists.** Mounted width, capacity, round-trip, clean unmount and successful 1-bit fallback
  remain **PENDING**; exact lines are in checklist 9 below.
- **Item-7 coordinator gate/serial status (2026-11-23)**: `./cb verify` completed **18 pass, 0 fail,
  0 skip**, including **226 CLI tests**, 47 host tests, real pin-drift on 12 byte-equal mirror files,
  `arduino:button_events` and `arduino:rgb_blink` compiled against the staged library, seven
  sdkconfig targets, and `flash+monitor+arduino` guards. `./cb arduino --upload rgb_blink` then
  uploaded successfully and printed the exact post-dim cycle and alive lines quoted in checklist 11.
  This is the item-7 build + RGB serial-half PASS; `button_events` upload and visible LED/gesture
  behavior remain PENDING.
- **Item-6 coordinator gate/serial status**: the full gate on the reviewed implementation completed
  **15 pass, 0 fail, 2 skip**; app and all six examples built, sdkconfig-assert passed all **seven**
  targets, host tests passed **47**, and CLI tests passed **152**. The 2026-11-23 hardware-session
  record gives checklist 10 a **serial-half PASS**: `./cb selftest` exited 0 with the seven ordered
  items, real battery/IMU values, `RESULT:PASS total=7 pass=5 fail=0 skip=2`, and
  `2 skipped (unverified)`. The full gate after the round-2 parser/target fixes, live/alive soak,
  dashboard visuals, mounted-card variant and root 0.5.0 regression remain **PENDING**; exact
  measured lines are in checklist 10 below.
- **Item-6 docs-lane checks (2026-11-24; supplementary, not the full gate)**: with
  `CB_BUILD_ROOT=build/kimi-k31-pro`, `./cb test` passed all **47** host tests (`# failed 0`),
  `python3 -m unittest discover -s tools/tests -t . -q` passed **152** tests, and
  `./cb build app 06_dashboard` passed both targets with the 0.5.0 BSP header (`06_dashboard.bin`
  **685168 bytes**). With the dashboard and all seven locks present, `./cb verify --fast` completed
  **7 pass, 0 fail, 10 skip**, including `deps-pins` on seven locks and the expected
  `build:06_dashboard` SKIP in fast mode. The coordinator's full gate and serial-half capture are
  recorded above; the final round-2 gate and the remaining hardware checks stay PENDING.
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
   Once `arduino/` exists, a missing/broken arduino-cli or missing/wrong pinned core is instead a
   **fatal** environment problem (exit 3); optional tier-2 LVGL absence stays informational.
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
   Item 5 adds **no new host tests**: `cb_sd.c` is IDF-only SDMMC/VFS/FATFS glue and IDF stubs are
   forbidden (SD9/R11b). No pure helper is introduced. The existing **47** tests, including all six
   SD pin values and GPIO uniqueness, remain the host regression suite; mount, fallback, VFS info,
   unmount and file IO require source review plus real-card evidence, not mocked host success.
   Item 6 likewise adds **no new pure C helper or host test**: `cb_bsp_part_result()` records from
   the real IDF-only `k_parts[]` init loop, which cannot be exercised without forbidden IDF stubs.
   Its build/source review and the coordinator dashboard capture are the evidence; the existing 47
   pure-C tests remain the host regression suite. The versioned self-test parser is Python instead.
3. `cli-tests` — the CLI's own unit tests:
   `python3 -m unittest discover -s tools/tests -t . -q` (a missing `tools/tests/` is a FAIL,
   owner deepseek-flash). Item 4's suite had **112 tests**. Item 5 added one missing-lock test and
   two required-target negatives (full + fast), making its suite **115 tests**. Item 6 adds the
   static `test_selftest.py` golden PASS / one-item RESULT:FAIL and malformed-output matrix,
   including prefixed+ANSI input, absent-SKIP arithmetic, target preference and trailing live/alive
   handling. Round 2 adds required-06 deletion protection, duplicate BEGIN/token and illegal-SKIP rejection,
   exact button/poller and per-item evidence checks, duplicate-key rejection, and the post-RESULT
   trailing drain and, in round 3, contract content for every `SELFTEST` line plus required SD
   mounted/width and numeric battery/IMU/LCD sanity. The final item-6 suite is **185 tests** at
   `83a525f`. Item 7 adds manifest/staging, pin-drift, audit-scope, doctor, compile/upload and
   missing-sketch negatives. Its initial full gate ran **226** tests; round 2 adds zero-sketch upload,
   missing-canonical and committed-fork negatives, bringing the current suite to **229 tests**. No
   board or network is needed for these Python tests.
4. `pin-drift` — stages through the sole `tools/cbchecks/arduino.py` parser, then byte-compares all
   **12** mirror files (seven headers + five implementations) against `components/cb_bsp/`.
   Mismatches report `pin-drift <relpath> differs from components/cb_bsp/…`; missing files,
   dropped manifest entries and stale copies FAIL. Tests mutate one byte and drop one required
   entry. **SKIP only while `arduino/` is absent.**
5. `audit-static` — forbidden constructs + absolute-path scan (scope below).
6. `deps-pins` — every committed `dependencies.lock` must resolve **lvgl 9.5.0**,
   **esp_lvgl_port 2.9.0**, **led_strip 3.1.1**, **idf 6.1.0**, and
   `components/cb_bsp/idf_component.yml` must pin those three exactly (no `^`/`~`/`>=` ranges).
   A missing/stale lock FAILs with a `./cb deps` hint — run `./cb deps` once after any
   manifest/lock change (ruling A9). Item 5 had six project locks; item 6 adds `06_dashboard`,
   making **seven** (root plus six examples). Each example lock is committed by the example owner
   after its first successful build; dependency versions themselves do not change.
7. `build:app` — builds the root app (`main/` "hello beeper").
8. `build:<example>` — one step per `examples/*`. Item 3 adds required steps
   `build:02_rgb_button` and `build:03_battery` after `build:01_lcd_hello`; item 4 adds
   `build:04_imu`, and item 5 adds required `build:05_sdcard` with the same missing-target FAIL
   semantics in full and fast modes. Item 6 adds required `build:06_dashboard` after them with the
   same deletion protection; its targeted build and fast-gate checks are recorded below.
9. `sdkconfig-assert` — per built target: OCT PSRAM, PSRAM 80 M, 16 MB flash, USB-Serial/JTAG
   console, custom partition table; plus the built partition binary exists and the table contains
   `factory,app,factory,0x10000,3M` and `storage,data,fat,0x310000,0x800000`.
   **For `app` and `01_lcd_hello` it additionally requires** (A9; key list quoted from
   `tools/cbchecks/build.py:SDKCONFIG_LVGL_KEYS`): `CONFIG_LV_CONF_SKIP=y`,
   `CONFIG_LV_COLOR_DEPTH_16=y`, `CONFIG_LV_COLOR_DEPTH=16`, `CONFIG_LV_OS_NONE=y`,
   `CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y`, `CONFIG_LV_FONT_MONTSERRAT_14=y`,
   `CONFIG_LV_USE_LABEL=y`, `CONFIG_LV_USE_IMAGE=y`; and **forbids** `CONFIG_LV_CONF_MINIMAL=y`.
   Absence of any `CONFIG_LV_` config in those two targets is a **FAIL, not a skip**. For every
   other target, including 02/03/04/05/06, the same eight keys are checked **whenever any `CONFIG_LV_*`
   entry is present**; cb_bsp's manifest normally makes them present even though those examples
   do not call LVGL. (`CONFIG_LV_OS_NONE=y` is deliberate: esp_lvgl_port owns the LVGL task;
   demanding an `LV_OS_FREERTOS`-style setting would fail a correct build.)
10. `arduino:<sketch>` — item 7 replaces the synthetic `arduino:*` SKIP with required
    `arduino:rgb_blink` and `arduino:button_events` compile steps after staging. A deleted required
    sketch FAILs rather than vanishing; `--fast` deliberately skips both compile steps.
11. `guards-no-board` — runs `./cb flash` / `./cb monitor` with `CB_FORCE_NO_PORTS=1`; both must
    exit **2** with the BOOT+RESET recovery hint, without invoking any build or esptool. Item 7 adds
    `./cb arduino rgb_blink --upload`: exactly one sketch is required, and the no-port guard must
    exit **2** before any staging/compile. Zero/multiple upload sketches are usage error 3; compile
    or upload failure is 1 and success is 0.
12. `report` — writes `${CB_BUILD_ROOT:-build}/verify-report.md` and `.json` (per-step status,
    durations, app sizes via `idf.py size --format json` when it succeeds, `.bin`-size fallback
    otherwise). `./cb report` prints the markdown.

**Item-1 strictness + item-3 R9b + item-4 R25 + item-5 SD13 + item-6 round 2 + item-7 AR12:**
`tests/host/Makefile`, `build:app`, `build:01_lcd_hello`, `build:02_rgb_button`,
`build:03_battery`, `build:04_imu`, `build:05_sdcard`, and `build:06_dashboard` are *required* —
missing files make those steps **FAIL**, not SKIP, even in `--fast` mode. Full and fast missing-06
tests both require detail `examples/06_dashboard missing (CMakeLists.txt not found)`, just as the
missing-05 tests require the equivalent 05 detail. SKIP is legitimate only for `pin-drift`,
`arduino:*` (no `arduino/` tree yet), build and dependent `sdkconfig-assert` steps deliberately
skipped by `--fast`, and `--offline` sub-checks when `unshare` is unavailable. With all item-6
projects present, a normal full gate still has the two expected Arduino-related SKIPs;
sdkconfig-assert covers **seven** targets (app + six examples). Item 7 additionally requires
`arduino/sketches/rgb_blink` and `arduino/sketches/button_events`: deleting either sketch is a
FAIL, while `--fast` skips both real `arduino:<sketch>` build steps. With `arduino/` present,
`pin-drift` is a PASS/FAIL check, not a SKIP, and a full gate no longer emits the synthetic
`arduino:*` SKIP.

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
  files under `main/`, `components/`, `examples/`, `tests/`, `tools/` (the whole `tools/` tree),
  and — from item 7 — **`arduino/`**. `docs/**` and `*.md` are **exempt** — documentation must be
  able to quote forbidden symbols (this file and [troubleshooting.md](troubleshooting.md) do exactly
  that). Generated `build/` staging is skipped, while committed `.ino`/`.h`/`.cpp` under `arduino/`
  obey the same rules as IDF sources.
- **Absolute-path rule**: no absolute home-directory paths in any tracked text file (docs use
  `~/…` form); single allowlist entry `eim_config.toml` (pre-existing, untouchable).
- Forbidden constructs (live in `tools/cbchecks/`): `driver/i2c.h`, `lcd_color_rgb_endian_t`,
  `LCD_RGB_ENDIAN_`, `lv_disp_drv_t`, `lv_disp_draw_buf_t`, `lv_scr_act(`, `ledcSetup(`,
  `ledcAttachPin(`, `neopixelWrite(`, `format_if_mount_failed = true`, and `ESP_ERROR_CHECK(`
  inside `cb_bsp` on optional hardware. Item 5 adds no scan. Independently, code review must see
  exactly one textual `esp_vfs_fat_sdmmc_mount()` in `cb_sd.c` and its explicit
  `format_if_mount_failed = false`; that single-site/no-format invariant is not a new machine rule.

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
- Arduino physical behavior. A staged compile and byte-identical mirror prove build/source parity,
  not LED appearance, button feel/gesture timing, battery accuracy, IMU behavior, or IDF-twin visual
  behavior. Those require checklist 11's upload/serial capture and human observation.

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
- [ ] 8. (item 4) **I2C/QMI8658 IMU — serial half PASS 2026-11-23; remaining checks below are
      PENDING.**
      - [x] **Bus/probe/init serial — PASS 2026-11-23**: observed `probe 0x6A no answer`, then
        `probe 0x6B answered`; WHO_AM_I was **0x05**, revision readback was **0x7C**, and init
        completed with `up addr=0x6B who=0x05 range=8g/512dps odr=120Hz`. The new `ctrl9 done` and
        `readback` boot lines are quoted below. Internal-pull-up electrical suitability is not
        inferred from these serial lines.
      - [x] **Measured ADDR_AI and CH9/CTRL9 fixes — PASS 2026-11-23**: CTRL1 auto-increment is
        **bit 6 = 0x40**, not 0x80. With 0x80 the measured burst repeated its first byte
        (`burst35=41 41 41 41 41 41`); with 0x40 it returned real data (`3F F1 69 03 38 FB`). The
        vendor CH9/CTRL9 **0x12 AHB-clock-gating command is also required**: without it the output
        registers returned a moving counter. The corrected macro, bounded CTRL9 completion wait and
        permanent readback line landed in **`a05fded`**.
      - [x] **Ready/repeating-sample/alive serial — PASS 2026-11-23**: observed
        `imu: ready addr=0x6B who=0x05`, two stable full sample lines approximately 2 s apart, and
        `imu: alive #1`. This proves the quoted serial behavior, not a longer cadence soak.

        Coordinator's final `./cb flash --target 04_imu` capture:

        ```text
        I (590) cb_imu: probe 0x6A no answer
        I (591) cb_imu: probe 0x6B answered
        I (596) cb_imu: ctrl9 cmd 0x12 done in 5 ms
        I (648) cb_imu: readback revision=0x7C ctrl1=0x40 ctrl2=0x26 ctrl3=0x56 ctrl7=0x03 ctrl9=0x12
        I (648) cb_imu: up addr=0x6B who=0x05 range=8g/512dps odr=120Hz
        I (648) imu: ready addr=0x6B who=0x05
        I (649) imu: ax=-0.921 ay=+0.212 az=-0.299 gx=-2.16 gy=-3.22 gz=+0.12 temp_c=+31.05
        I (2650) imu: ax=-0.923 ay=+0.211 az=-0.300 gx=-2.27 gy=-3.31 gz=+0.16 temp_c=+31.03
        I (6652) imu: alive #1
        ```
      - [ ] **R23 resting acceptance** — with the board resting flat, the largest of
        `|ax|`, `|ay|`, `|az|` must be **0.9–1.1 g**; each of the other two accel axes must be
        **<0.15 g**; every `|gx|`, `|gy|`, `|gz|` must be **<2.00 dps**; and `temp_c` must be
        **10–50 °C**. Record actual numeric samples. These are sanity thresholds, not calibration,
        full-scale accuracy or axis-orientation proof. **Measured in this capture**:
        `ax=-0.921 ay=+0.212 az=-0.299`, magnitude **0.99 g** concentrated on X with the board
        resting on its side; gyro was within approximately **3 dps** of zero and temperature was
        stable near **31 °C**. This is functional serial evidence, but the side-resting capture does
        not meet every strict threshold above (`ay`/`az` and `gx`/`gy`), so no R23 acceptance or
        accuracy/bias-calibration PASS is claimed.
      - [ ] **Degraded paths** — if neither address ACKs, record one
        `W imu: no IMU found (probed 0x6A/0x6B), samples skipped` and continuing alive lines with
        no ready/sample lines; mark this **SKIP/degraded, never PASS**. On a read error, require
        one `W imu: read failed: %s`, stopped polling and continuing alive lines. An unobserved
        failure path remains code/build-only.
      - [ ] **Root regression serial** — fresh `app` boot must contain
        `cb_bsp=0.3.0 init(CB_PART_ALL)=ESP_OK` and continuing `hello_beeper: alive #N`; only SD
        should remain `not implemented`. This does not prove optional IMU health.
      - [ ] **Human/electrical checks** — deliberate motion response (tilt/shake changes the vector
        as expected), full-scale accuracy and **accuracy/bias calibration** remain **PENDING (needs
        human)**. Axis orientation and internal-pull-up signal integrity at 400 kHz also remain
        PENDING even after the serial checks above pass.
- [ ] 9. (item 5) **TF card over SDMMC — no-card degraded path PASS 2026-11-23; mounted path
      PENDING. A mount error is not proof that no card exists.**
      - [x] **No-card mount/degraded path — PASS 2026-11-23**: `05_sdcard` made exactly one direct
        `cb_sd_init()` call (one 4-bit attempt and at most one 1-bit fallback), without calling
        `cb_bsp_init` at all. The 4-bit attempt timed out, the 1-bit retry timed out, and the
        example emitted its frozen no-card WARN once, then kept its heartbeat. No ready,
        round-trip or success-unmount line appeared. The root app separately owns the optional
        `cb_bsp_init` suppression check. This is evidence of **no card detected on this bench**,
        not proof of physical slot emptiness or mounted functionality. Capture:

        ```text
        W (565) cb_sd: 4-bit mount failed: ESP_ERR_TIMEOUT — retrying 1-bit
        W (595) cb_sd: mount failed: ESP_ERR_TIMEOUT (no card?)
        W (596) sdcard: no card or mount failed: ESP_ERR_TIMEOUT
        I (10596) sdcard: alive #1
        ```
      - [ ] **Mounted ready/capacity — PENDING (needs a card)**: require
        `sdcard: ready width=%d mount=/sdcard total_bytes=%llu free_bytes=%llu` and record the
        negotiated width/raw-byte values verbatim. A 4-bit success would not prove fallback. If
        `cb_sd_info` fails, the example logs `roundtrip failed: %s` and unmounts instead of
        printing ready; that is not a full ready+IO PASS.
      - [ ] **Successful 1-bit mounted fallback — PENDING**: the no-card capture proves the
        4-bit failure and 1-bit retry occurred, but no `up ... width=1` followed. A mounted fallback
        PASS requires `cb_sd: 4-bit mount failed: %s — retrying 1-bit` followed by
        `cb_sd: up mount=/sdcard width=1 max_freq_khz=20000` on a real card.
      - [ ] **Exclusive 4096-byte round-trips — PENDING (needs a card)**: require three
        `sdcard: roundtrip ok path=/sdcard/cb05_rt%03u.tmp bytes=4096 verified=1 cleanup=deleted`
        lines, 10 s apart, with actual 000–099 paths. Source review confirms
        `O_CREAT | O_EXCL | O_WRONLY`, `(uint8_t)(i * 31 + 7)`, byte-for-byte read-back compare,
        and `unlink()` only of the newly created path. `cleanup=deleted` is accepted only after
        successful unlink; no other file is enumerated or touched. Collision, partial-write and
        unlink-failure paths remain PENDING unless actually exercised.
      - [ ] **Unmount — PENDING (needs a mounted card)**: mounted runs must end with
        `cb_sd: unmounted`. On `cb_sd: unmount failed: %s`, the driver keeps mounted=true rather
        than claiming cleanup; that error path is unverified until injected. `cb_sd_info()` before
        mount returns `ESP_ERR_INVALID_STATE` with one-shot `cb_sd: not mounted` and untouched
        outputs.
      - [ ] **Heartbeat cadence and root regression — PENDING**: `alive #1` proves liveness after
        mount failure, not by itself the first-~5 s/repeating-~5 s cadence. Require timestamped
        intervals before ticking cadence. Fresh `app` boot must show
        `cb_bsp=0.4.0 init(CB_PART_ALL)=ESP_OK` and continuing `hello_beeper: alive #N`; optional
        SD failure does not prove SD health.
      - [x] **No-format code invariant — build/code-verified**: exactly one mount helper/site has
        explicit `format_if_mount_failed = false`, and the existing forbidden `= true` rule passed
        in the 14-step gate. This proves the source-level no-auto-format contract without a
        destructive card test.
      - [ ] **Remaining hardware checks — PENDING (needs card/human)**: sustained IO, hot removal,
        internal-pull-up signal integrity, collision/error paths and user-data preservation.
- [ ] 10. (item 6) **Seven-part self-test + LVGL dashboard — serial-half PASS 2026-11-23;
      final round-2 gate, live/alive soak, visuals, mounted card, root regression and human checks
      PENDING.** The gold-standard bench report is the following no-card/no-finger sequence. Its
      arithmetic is five PASS, zero FAIL, two SKIP; both SKIPs remain unverified even though the
      overall command exits 0.

      ```text
      SELFTEST BEGIN version=1 cb_bsp=0.5.0
      ITEM:lifecycle PASS init=ESP_OK
      ITEM:lcd PASS lvgl=9.5.0 frames=1
      ITEM:rgb PASS cap_percent=20
      ITEM:button SKIP reason=needs-human poller=running
      ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1
      ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99
      ITEM:sd SKIP reason=no-card
      RESULT:PASS total=7 pass=5 fail=0 skip=2
      ```

      - [x] **Reviewed-implementation full gate + parser suite — PASS**: the coordinator's
        `CB_BUILD_ROOT=build/coordinator ./cb verify` completed **15 pass, 0 fail, 2 skip**;
        `build:06_dashboard` was present, sdkconfig-assert covered **seven** targets, host tests
        passed **47**, and the static Python suite passed **152**. The suite covers the golden PASS,
        a valid one-item RESULT:FAIL, and malformed version/count/order/tokens/reasons/evidence /
        arithmetic/RESULT cases without a board or IDF stubs. The BSP result recorder remains
        IDF-only lifecycle bookkeeping, so no new pure-C host test is claimed.
      - [ ] **Final round-2 full gate — PENDING**: rerun after the required-target, parser-strictness
        and trailing-drain fixes, then replace this with the actual totals. The parser must reject a
        second BEGIN, duplicate/unknown tokens, every SKIP outside button/SD, button without exact
        `poller=running`, missing or false per-item evidence (including `calibrated=0` battery PASS),
        arithmetic/terminal violations, and any trailing ITEM/RESULT/SELFTEST in the ~2 s drain.
        The docs-lane `python3 -m unittest discover -s tools/tests -t . -q` rerun at `acd83ef`
        passed **170/170** tests; only the coordinator's new full gate remains unrecorded.
      - [x] **Golden serial + process result — PASS 2026-11-23**: the coordinator flashed
        `06_dashboard` and ran `./cb selftest`; it exited **0** and printed
        **`2 skipped (unverified)`**. The measured report preserves the exact gold shapes/order and
        arithmetic, with real battery **4209 mV**, pin **1393 mV**, calibrated=1, and IMU **0x6B**,
        WHO **0x05**, resting magnitude **1.00 g**:

        ```text
        SELFTEST BEGIN version=1 cb_bsp=0.5.0
        ITEM:lifecycle PASS init=ESP_OK
        ITEM:lcd PASS lvgl=9.5.0 frames=1
        ITEM:rgb PASS cap_percent=20
        ITEM:button SKIP reason=needs-human poller=running
        ITEM:battery PASS battery_mv=4209 pin_mv=1393 calibrated=1 plausible=1
        ITEM:imu PASS addr=0x6B who=0x05 mag_g=1.00
        ITEM:sd SKIP reason=no-card
        RESULT:PASS total=7 pass=5 fail=0 skip=2
        live bat=4209mV plausible=1 imu_mag=1.00g addr=0x6B
        [PASS] selftest RESULT:PASS total=7 pass=5 fail=0 skip=2
        cb: 2 skipped (unverified)
        ```

        This verifies the serial grammar, ordering, arithmetic, calibrated battery snapshot, IMU
        identity/resting reading, one immediate post-RESULT live line, and CLI exit/SKIP behavior.
        It does not verify the two SKIPs, visual pixels, gestures, mounted SD, or live/alive
        cadence. `RESULT:PASS` requires `fail=0`; RESULT:FAIL and malformed/timeout output exit 1;
        no port exits 2 before build/flash. Target preference is 06 → 07 → 08, with no new flags.
      - [ ] **Live/alive soak — PENDING**: RESULT ends the contract, not serial output. In a separate
        monitor capture of at least 15 s after the self-test, require the combined live line every
        ~2 s and `dashboard: alive #N` every ~5 s with accumulating cadence and no WDT/reset. A
        valid example using the same snapshot is
        `live bat=4221mV plausible=1 imu_mag=0.99g addr=0x6B`; unavailable fields are exactly
        `bat=NAmV`, `imu_mag=NAg`, and/or `addr=0x00`, never stale data. Record timestamps and
        actual numbers rather than treating this example as evidence.
      - [ ] **Dashboard visual check (human) — PENDING**: confirm the `cb_bsp=0.5.0` title, all
        seven legible status rows in order, live battery mV/plausible and IMU magnitude/address
        rows, and a changing alive tick. Check clipping, colours, geometry and tearing. Serial
        `frames=1` proves the example's first LVGL flush only; it cannot tick any visual box.
      - [ ] **Mounted-card variant — PENDING (needs a known usable card)**: if available, repeat
        self-test with the card state known and record the actual SD evidence/width and recomputed
        RESULT. Mounted state is the only item-6 SD claim; capacity, file round-trips, successful
        1-bit fallback, unmount, hot removal and user-data preservation remain checklist-9 work.
        The golden `reason=no-card` is an unverified no-card/mount state, not proof of physical slot
        emptiness because the board has no card-detect pin.
      - [ ] **Root 0.5.0 regression — PENDING**: flash `app` separately and capture
        `cb_bsp=0.5.0 init(CB_PART_ALL)=ESP_OK` plus continuing `hello_beeper: alive #N`. This
        proves the new build stamp/strict lifecycle only, not optional-part health.
      - [ ] **Human/electrical limits — remain PENDING unless separately observed**: RGB visible
        colour/brightness, BOOT PRESS/RELEASE/CLICK/DOUBLE_CLICK/LONG_PRESS gestures, battery
        multimeter accuracy and physical cell presence, IMU motion/axis/full-scale accuracy/bias
        calibration and signal integrity, and every uninjected driver failure path. Button is
        always `SKIP reason=needs-human poller=running`; a press window never makes it PASS. No
        Wi-Fi/eighth item is added, and SKIP is never hardware verification.
- [ ] 11. (item 7) **Arduino parity — gate and RGB serial-half PASS 2026-11-23; button upload and
      visual/physical parity PENDING.** The two sketches are `rgb_blink` and `button_events`; the library has only
      `CBBoard.h`, `mirror.txt` and `library.properties`, never an `.ino` or a second committed
      mirror copy.
      - [x] **Gate/staging — PASS 2026-11-23**: coordinator full `./cb verify` completed
        **18 pass, 0 fail, 0 SKIP**, with real `pin-drift` PASS (12 byte-equal files),
        `audit-static` including `arduino/`, `arduino:rgb_blink` and `arduino:button_events`
        compiled against the staged library, and no synthetic `arduino:*` SKIP. Seven sdkconfig
        targets, 47 host tests and **226 CLI tests** passed; logs confirm the exact locked FQBN
        and staged `--libraries` path. Deleting a required sketch FAILs; fast mode alone skips the
        builds. Missing arduino-cli/core 3.3.12 is fatal doctor exit 3 once `arduino/` exists.
        Python negatives cover a mutated byte, dropped manifest entry, missing sketch and
        two-sketch upload rejection. The round-2 docs-lane rerun passed **229/229** tests after
        adding zero-sketch upload, missing-canonical and committed-fork negatives.
      - [x] **No-board upload guard — PASS**: the full gate's `guards-no-board` step reports
        `flash+monitor+arduino exit 2 with the recovery hint (CB_FORCE_NO_PORTS=1)`, proving the
        Arduino guard fires before staging/building. Zero or multiple sketches with `--upload`
        exits **3**. Upload success is not accepted as serial evidence by itself.
      - [x] **RGB serial half — PASS 2026-11-23**: coordinator ran
        `./cb arduino --upload rgb_blink`, recorded exit 0, and captured the following raw Type-C
        `Serial` lines. Values 51 are the shared `cb_rgb_math_dim` 20% peak cap applied in the
        Arduino sketch, not a hand-maintained RGB implementation:

        ```text
        rgb_blink: cycle 1 r=51 g=0 b=0
        rgb_blink: cycle 2 r=0 g=51 b=0
        rgb_blink: cycle 3 r=0 g=0 b=51
        rgb_blink: alive #1
        rgb_blink: cycle 4 r=51 g=51 b=51
        rgb_blink: cycle 5 r=0 g=0 b=0
        ```

        This proves compile, guarded upload and the printed post-dim values/alive line, not visible
        hue/brightness or visual IDF-twin parity.
      - [ ] **Button serial half — PENDING unless separately flashed**: if the coordinator chooses
        `button_events` instead/additionally, require `button_events: ready poll_ms=10` once,
        `button_events: button event <TOKEN>` for observed FSM events, and
        `button_events: alive #%u` every ~5 s. Valid tokens are PRESS, RELEASE, CLICK,
        DOUBLE_CLICK and LONG_PRESS. No event may be fabricated for an unattended run.
      - [ ] **Visual/physical parity — PENDING (needs human)**: LED hue/brightness/brownout,
        button feel and physical gesture timing, and visual “behaves like its IDF twin” claims.
        Numeric `r/g/b` lines prove shared dim math and values passed to `rgbLedWrite`, not visible
        brightness. FSM event lines do not prove button feel. Unflashed sketch behavior, battery /
        IMU physical behavior and long-run stability also remain PENDING. Pin-drift proves source
        identity, not behavioral identity.
- [ ] 12. (item 8+) Wi-Fi scan lists APs; BLE scan lists advertisers.

Unticked boxes are **not hardware-verified**; build/code status is stated above, and human-only
checks stay pending even after a successful build. Ticked boxes record measured facts with dates.
