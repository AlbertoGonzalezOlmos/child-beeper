# Verification — what is proven, how, and what still needs the board

> Status: build-verified toolkit, item 1 (2026-11-08). **Nothing in this repository is
> hardware-verified** — no board was attached when it was written. See the hardware-day checklist
> at the bottom.

This repo distinguishes two levels of proof, and every doc/report must say which one applies:

| Level | Meaning | How it is established |
|---|---|---|
| **build-verified** | Compiles and links cleanly on this machine with the pinned toolchain; static checks pass | `./cb verify` (below) |
| **hardware-verified** | Observed working on the physical board | Only by running the hardware-day checklist below and recording results |

Rule (also in `AGENTS.md`): *never claim hardware-verified without a board.*

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
5. `build:app` — builds the root app (`main/` "hello beeper").
6. `build:<example>` — one step per `examples/*` (item 1: `build:01_lcd_hello`).
7. `sdkconfig-assert` — per built target: OCT PSRAM, PSRAM 80 M, 16 MB flash, USB-Serial/JTAG
   console, custom partition table; plus the built partition binary exists and the table contains
   `factory,app,factory,0x10000,3M` and `storage,data,fat,0x310000,0x800000`.
8. `arduino:<sketch>` — **SKIP while `arduino/` is absent (all of item 1).**
9. `guards-no-board` — runs `./cb flash` / `./cb monitor` with `CB_FORCE_NO_PORTS=1`; both must
   exit **2** with the BOOT+RESET recovery hint, without invoking any build or esptool.
10. `report` — writes `${CB_BUILD_ROOT:-build}/verify-report.md` and `.json` (per-step status,
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
forbids fetch commands; if `unshare` is unavailable the sub-checks report SKIP with explicit wording
("offline namespace unavailable — NOT proven offline"). It never uses `IDF_COMPONENT_MANAGER=0`
(that disables managed dependencies entirely and would make builds fail for the wrong reason).
Offline builds work because the dependency pins (lvgl 9.5.0, esp_lvgl_port 2.9.0, led_strip 3.1.1)
are all present in the local caches — see [troubleshooting.md](troubleshooting.md#component-manager-and-offline-builds).

## `audit-static` scope (what it scans)

- **Construct rules** (forbidden list below) scan only `.c .h .cpp .hpp .ino CMakeLists.txt .cmake`
  files under `main/`, `components/`, `examples/`, `tests/`, `tools/cbchecks/tests/`.
  `docs/**` and `*.md` are **exempt** — documentation must be able to quote forbidden symbols
  (this file and [troubleshooting.md](troubleshooting.md) do exactly that).
- **Absolute-path rule**: no absolute home-directory paths in any tracked text file (docs use
  `~/…` form); single allowlist entry `eim_config.toml` (pre-existing, untouchable).
- Forbidden constructs (live in `tools/cbchecks/`): `driver/i2c.h`, `lcd_color_rgb_endian_t`,
  `LCD_RGB_ENDIAN_`, `lv_disp_drv_t`, `lv_disp_draw_buf_t`, `lv_scr_act(`, `ledcSetup(`,
  `ledcAttachPin(`, `neopixelWrite(`, `format_if_mount_failed = true`, and `ESP_ERROR_CHECK(`
  inside `cb_bsp` on optional hardware.

## What `./cb verify` does **not** prove

- Any runtime behaviour: display output, colours, x-gap correctness, IMU data, battery accuracy,
  SD I/O, button feel, RGB brightness, radio behaviour — all need the board (checklist below).
- That frozen constants match silicon (e.g. ADC unit / SPI host enum values are contract-checked by
  host tests, but the electrical truth is a hardware-day item).
- Real port enumeration: `guards-no-board` proves the guard *logic* via `CB_FORCE_NO_PORTS=1`, not
  that the board enumerates as `/dev/ttyACM0`.
- Anything under `arduino/` until item 7 lands.

## Hardware-day checklist (run when a board is attached; record results here)

Prerequisites: USB-C **data** cable; user in `dialout`; see [flashing.md](flashing.md).
Procedure per line: run the command, compare with "expected", tick and fill in date/result.

- [ ] 1. Board enumerates: `ls /dev/ttyACM*` shows a port (`dmesg | tail` confirms USB-Serial/JTAG).
      Result/date: ＿＿＿
- [ ] 2. `./cb flash --target app && ./cb monitor` → banner `hello beeper`, `cb_bsp 0.1.0 / ESP-IDF
      v6.1...`, WARN lines for not-yet-implemented parts, `alive #N` heartbeat every 2 s.
      (In item 1, `cb_bsp_init(CB_PART_ALL)` returning `ESP_ERR_NOT_SUPPORTED` is **expected**.)
- [ ] 3. Recovery: hold **BOOT**, tap **RESET**, release **BOOT** → download mode; flash succeeds.
- [ ] 4. (item 2+) `./cb flash --target 01_lcd_hello` → colour bars reach all four edges, corner
      markers visible, no 34-px shift, red bar is red (proves x-gap/colour order/inversion).
- [ ] 5. (item 3+) Battery reading within ±5 % of a multimeter; RGB visible but dim-capped;
      button: CLICK arrives ~350 ms after release, a double click yields RELEASE then DOUBLE_CLICK,
      hold ≥800 ms yields LONG_PRESS once and no click on release.
- [ ] 6. (item 4+) IMU: log shows which address answered the probe (**0x6A or 0x6B**), WHO_AM_I
      0x05, ≈1 g on the resting axis, gyro ≈0.
- [ ] 7. (item 5+) SD: mount logs the bus width used (4-bit, or 1-bit fallback); write/read
      round-trip passes; **no card is ever auto-formatted**.
- [ ] 8. (item 6+) `./cb selftest` → `SELFTEST`/`ITEM:`/`RESULT:` lines, `RESULT` all PASS
      (serial contract frozen in item 6).
- [ ] 9. (item 7+) One Arduino sketch flashed via `./cb arduino` behaves like its IDF twin;
      `Serial` output visible over Type-C (CDC on boot).
- [ ] 10. (item 8+) Wi-Fi scan lists APs; BLE scan lists advertisers.

Until boxes are ticked with dates, every feature in this repo is **build-verified only**.
