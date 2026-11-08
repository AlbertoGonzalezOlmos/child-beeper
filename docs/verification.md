# Verification — what is proven, how, and what still needs the board

> Status (updated 2026-11-08, board attached): the toolkit is build-verified **and** the first
> hardware session is done — flash, console and an 11-minute soak are now **hardware-verified**;
> everything else remains build-verified only. See "Hardware status" below and the checklist at the
> bottom.

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

**hardware-UNAVAILABLE (with reasons)**
- **Early boot lines are not observable** on the USB-Serial/JTAG console: after a reset the native
  USB peripheral re-enumerates and the host re-attaches only ~1 s later, so ROM/bootstrap/
  bootloader output and the app's banner/WARN lines are dropped before anyone is listening. The
  root app therefore prints a **one-shot boot summary on its first heartbeat**
  (`boot summary: reset=… cb_bsp=… init(CB_PART_ALL)=… idf=…`) — observe that line instead of the
  true early boot log.
- **BOOT+RESET recovery drill** (hold BOOT → tap RESET → release) needs a physical finger on the
  buttons — still pending.

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
   `cb_math` (duty clamp, LE decode, scale math, ADC conversion) and `cb_button_fsm` (single click,
   double click, long press, bounce, injected time).
3. `pin-drift` — byte-compares Arduino copies of the shared headers against
   `components/cb_bsp/include/`. **SKIP while `arduino/` is absent (all of item 1).**
4. `audit-static` — forbidden constructs + absolute-path scan (scope below).
5. `cli-tests` — the CLI's own unit tests:
   `python3 -m unittest discover -s tools/tests -t . -q` (a missing `tools/tests/` is a FAIL,
   owner deepseek-flash).
6. `build:app` — builds the root app (`main/` "hello beeper").
7. `build:<example>` — one step per `examples/*` (item 1: `build:01_lcd_hello`).
8. `sdkconfig-assert` — per built target: OCT PSRAM, PSRAM 80 M, 16 MB flash, USB-Serial/JTAG
   console, custom partition table; plus the built partition binary exists and the table contains
   `factory,app,factory,0x10000,3M` and `storage,data,fat,0x310000,0x800000`.
9. `arduino:<sketch>` — while `arduino/` is absent (all of item 1) a single **synthetic step id
   `arduino:*`** is emitted as SKIP instead of per-sketch steps.
10. `guards-no-board` — runs `./cb flash` / `./cb monitor` with `CB_FORCE_NO_PORTS=1`; both must
    exit **2** with the BOOT+RESET recovery hint, without invoking any build or esptool.
11. `report` — writes `${CB_BUILD_ROOT:-build}/verify-report.md` and `.json` (per-step status,
    durations, app sizes via `idf.py size --format json` when it succeeds, `.bin`-size fallback
    otherwise). `./cb report` prints the markdown.

**Item-1 strictness (ruled):** `tests/host/Makefile`, `build:app` and `build:01_lcd_hello` are
*required* — missing files make those steps **FAIL**, not SKIP. SKIP is legitimate only for
`pin-drift`, `arduino:*` (no `arduino/` tree yet) and `--offline` sub-checks when `unshare` is
unavailable.

**Exit codes:** a FAIL in `doctor` → **3**; any other FAIL → **1**; all PASS/SKIP → **0**.
(Globally: 0 ok, 1 build/test failure, 2 board/port absent, 3 usage/environment.)

**`--fast`** skips every build step (same ids, marked SKIP) — useful while other slices are landing.

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
- Early-boot log content (dropped by USB re-enumeration — use the one-shot boot-summary line).
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
      (`alive #323`) with no WDT/reset — **verified 2026-11-08**. The true early-boot lines
      (banner/WARN) are dropped by USB re-enumeration — see "Hardware status" above.
- [ ] 3. **Boot summary**: after a fresh flash/reset, the first heartbeat carries the one-shot
      `boot summary: reset=… cb_bsp=0.1.0 init(CB_PART_ALL)=ESP_ERR_NOT_SUPPORTED idf=…` line
      (pending — observe at the next flash).
- [x] 4. Guard logic with the board attached: `CB_FORCE_NO_PORTS=1 ./cb flash` → exit **2** +
      BOOT/RESET hint — **verified** via `./cb verify` step `guards-no-board`.
- [ ] 5. Recovery: hold **BOOT**, tap **RESET**, release **BOOT** → download mode; flash succeeds
      (**pending — needs a physical press**).
- [ ] 6. (item 2+) `./cb flash --target 01_lcd_hello` → colour bars reach all four edges, corner
      markers visible, no 34-px shift, red bar is red (proves x-gap/colour order/inversion).
- [ ] 7. (item 3+) Battery reading within ±5 % of a multimeter; RGB visible but dim-capped;
      button: CLICK arrives ~350 ms after release, a double click yields RELEASE then DOUBLE_CLICK,
      hold ≥800 ms yields LONG_PRESS once and no click on release.
- [ ] 8. (item 4+) IMU: log shows which address answered the probe (**0x6A or 0x6B**), WHO_AM_I
      0x05, ≈1 g on the resting axis, gyro ≈0.
- [ ] 9. (item 5+) SD: mount logs the bus width used (4-bit, or 1-bit fallback); write/read
      round-trip passes; **no card is ever auto-formatted**.
- [ ] 10. (item 6+) `./cb selftest` → `SELFTEST`/`ITEM:`/`RESULT:` lines, `RESULT` all PASS
      (serial contract frozen in item 6).
- [ ] 11. (item 7+) One Arduino sketch flashed via `./cb arduino` behaves like its IDF twin;
      `Serial` output visible over Type-C (CDC on boot).
- [ ] 12. (item 8+) Wi-Fi scan lists APs; BLE scan lists advertisers.

Unticked boxes are **build-verified only**; ticked boxes record measured hardware facts with dates.
