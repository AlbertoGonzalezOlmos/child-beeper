# Arduino path — staged CBBoard library + parity sketches

> Status: item-7 AR1–AR17 contracts are frozen. The two sketches are built with the locked Arduino
> toolchain through a generated, per-agent staging tree; build and hardware results are recorded in
> [verification.md](verification.md). Nothing on this page turns an upload or a numeric serial value
> into visual/physical parity with the IDF examples.

## Pinned toolchain

| Component | Required value |
|---|---|
| arduino-cli | **1.4.1** (`~/.local/bin/arduino-cli`, or `$CB_ARDUINO_CLI`) |
| Arduino core | **`esp32:esp32` 3.3.12** — not the older `arduino:esp32` 2.0.18 core also present on some machines |
| Arduino IDE | `/usr/bin/arduino` (optional; `./cb arduino` and `./cb verify` use the CLI) |
| External libraries | **None** for `rgb_blink` and `button_events`; tier-2 LVGL remains a separate explicit install |

Once `arduino/` exists, `./cb doctor` treats a missing/broken arduino-cli or missing/wrong pinned core
as a **fatal** environment error (exit 3). Before the tree existed these were warnings. Tier-2 LVGL
absence remains informational because neither item-7 sketch uses it.

## Locked FQBN

Every compile and upload uses exactly:

```text
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc,PartitionScheme=default
```

| Option | Value | Reason |
|---|---|---|
| `PSRAM` | `opi` | 8 MB octal PSRAM |
| `FlashSize` | `16M` | 16 MB flash |
| `CDCOnBoot` | `cdc` | Arduino `Serial` appears on Type-C |
| `USBMode` | `hwcdc` | Hardware CDC/JTAG, matching the native USB port |
| `PartitionScheme` | `default` | Measured core option; it is not the IDF custom CSV and no item-7 sketch depends on the IDF FAT layout |

Arduino IDE equivalents are Board **ESP32S3 Dev Module**, OPI PSRAM, 16 MB flash, USB CDC On Boot
enabled, Hardware CDC and JTAG, and the default partition scheme. The CLI is authoritative for the
gate.

## Single source of truth: `mirror.txt`

Canonical pins and pure functions live only in `components/cb_bsp/`. The committed manifest is
`arduino/libraries/CBBoard/mirror.txt`, read **only** by `tools/cbchecks/arduino.py`. Build, install,
pin-drift and tests consume that parser's API; no second parser or mirror search path exists.

Each non-comment line is one component-relative `include/...` or `src/...` path. Blank lines and `#`
comments are allowed. Absolute paths, `..`, paths outside those directories, duplicate entries and
missing canonical files fail closed. The destination is always staged `src/<basename>`.

The manifest contains **exactly 12 entries**, seven public headers and their five required link-time
implementations:

```text
include/cb_pins.h
include/cb_math.h
include/cb_button_fsm.h
include/cb_qmi8658_regs.h
include/cb_rgb_math.h
include/cb_battery_math.h
include/cb_imu_math.h
src/cb_math.c
src/cb_button_fsm.c
src/cb_rgb_math.c
src/cb_battery_math.c
src/cb_imu_math.c
```

Deleting an entry is a failure, not a smaller valid mirror. Header-only staging would leave undefined
references, so the five `.c` files are mandatory. `cb_pattern` is not in R25 and is not mirrored.
There are no IDF/FreeRTOS includes in this set; public function headers carry `extern "C"` guards so
the C implementations link with C++ sketches.

## What is committed and what is staged

Committed under `arduino/`:

- `libraries/CBBoard/CBBoard.h` — umbrella only: seven named mirror-header includes and comments
  documenting the IDF-to-Arduino mapping; no logic or forked driver;
- `libraries/CBBoard/library.properties` — ordinary Arduino metadata, with no external dependency;
- `libraries/CBBoard/mirror.txt` — the 12-entry manifest above;
- `sketches/rgb_blink/` and `sketches/button_events/` — the two sketches.

No mirrored `cb_*.h` or `cb_*.c` is committed under `arduino/`. The library must never contain an
`.ino`: sketch discovery searches library examples as well as `arduino/sketches/`, so an example there
would silently become another build target.

`./cb arduino` stages into the current agent's build root before compiling:

```text
${CB_BUILD_ROOT:-build}/arduino/
├── libraries/
│   └── CBBoard/
│       └── src/
│           ├── CBBoard.h                 # staged committed umbrella
│           ├── cb_pins.h                 # staged canonical mirror files
│           ├── cb_math.h / cb_math.c
│           ├── cb_button_fsm.h / cb_button_fsm.c
│           ├── cb_qmi8658_regs.h
│           ├── cb_rgb_math.h / cb_rgb_math.c
│           ├── cb_battery_math.h / cb_battery_math.c
│           └── cb_imu_math.h / cb_imu_math.c
├── rgb_blink/                           # sketch-specific compile artifacts
└── button_events/                       # independent artifacts, not shared mutable state
```

Staged copies are generated, never committed and never hand-edited. Build/export refreshes replace
the staging state rather than accumulating stale mirror files. Use a separate `CB_BUILD_ROOT` for
each agent, as with IDF builds.

Compile uses the **staged libraries parent**, not the committed library directory or the sketchbook:

```bash
arduino-cli compile \
  -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc,PartitionScheme=default" \
  --libraries "${CB_BUILD_ROOT}/arduino/libraries" \
  --build-path "${CB_BUILD_ROOT}/arduino/rgb_blink" \
  --warnings all --no-color \
  arduino/sketches/rgb_blink
```

`--build-path` is artifact output only. In normal use call the toolkit instead:

```bash
export CB_BUILD_ROOT=build/<your-name>
./cb arduino                 # stage once, then compile both sketches
./cb arduino rgb_blink       # stage, then compile only this sketch
./cb arduino button_events
```

Arduino core compilation can take minutes even with a warm cache; the independent build paths favor
correct artifact isolation over clever object sharing. `./cb verify` creates one required
`arduino:<sketch>` step per sketch. A deleted sketch is a gate failure, not an invisible SKIP; fast
mode deliberately skips the compile steps.

## Pin-drift and static audits

Verify step **`pin-drift`** is now a real check whenever `arduino/` exists:

- every staged mirror file is compared byte-for-byte with its canonical `components/cb_bsp/` source.
  Pin-drift bootstraps an absent/incomplete stage, but otherwise compares the existing stage
  **read-only**, so a stale or edited staged copy is reported rather than silently repaired. Run
  `./cb arduino` to refresh staging and rebuild before rerunning the gate;
- a mismatch fails with detail shaped as
  `pin-drift <staged-relative-path> differs from components/cb_bsp/<path>`;
- a missing canonical file, a dropped manifest entry (set size not 12), or a stale copy is a failure;
- Python negatives mutate one byte and drop one manifest line, in addition to missing-file/path
  validation cases. `arduino/` absence is the only remaining SKIP case, retained for pre-item-7 trees.

The `audit-static` construct scan now includes committed `arduino/` sources, including `.ino`, `.h`
and `.cpp`. Thus `neopixelWrite(`, `ledcSetup(`, `ledcAttachPin(`, LVGL 8 symbols, legacy I2C and
`format_if_mount_failed = true` are enforced there too. The generated `build/` tree is excluded;
documentation remains exempt so it can quote the forbidden vendor APIs.

## Upload: one sketch, guarded before building

Use exactly one sketch for an upload:

```bash
./cb arduino rgb_blink --upload [--port /dev/ttyACM0]
```

Zero or multiple sketches with `--upload` is a usage error (**3**). Upload performs these steps:

1. `guard_port()` runs **before staging or building**. Without a port it exits **2** and prints the
   same BOOT+RESET recovery hint as `./cb flash`, with no compile side effects.
2. The mirror is staged and the selected sketch compiles with the locked FQBN and per-sketch build
   path. Staging/compile failure exits **1**; missing toolchain/environment or bad sketch input exits
   **3**.
3. `arduino-cli upload` reuses that compiled `--build-path` output with the same FQBN and guarded
   port. Upload failure exits **1**; success exits **0**.

The no-board gate has an upload arm proving exit 2. An upload success line is not proof that `Serial`
output appeared; the coordinator captures USB-CDC output separately. Agents never run the upload arm
on hardware.

## `arduino-install` and the sketchbook

`./cb arduino-install` is an **optional IDE export**, not a build prerequisite. It stages first and
copies the complete staged CBBoard (umbrella + all 12 mirror files) into
`${ARDUINO_SKETCHBOOK:-~/Arduino}/libraries/CBBoard`, replacing that generated destination. Do not
hand-edit the exported copy. Gate builds use only the per-agent staged `--libraries` path and never
depend on sketchbook state.

`./cb arduino --install-libs` is unrelated: it remains the explicit, network-dependent tier-2 LVGL
installation path and refuses offline mode. No item-7 sketch needs that install.

## Sketch serial contracts

These are raw Arduino `Serial` payloads, not ESP_LOG lines. RGB values are the post-
`cb_rgb_math_dim` values passed to the core LED API. Button event names are the existing uppercase
FSM tokens.

### `rgb_blink`

Every **2 s**:

```text
rgb_blink: cycle %u r=%u g=%u b=%u
```

The first five steps use the shared 20% peak cap (full-scale 255 becomes 51):

```text
rgb_blink: cycle 1 r=51 g=0 b=0
rgb_blink: cycle 2 r=0 g=51 b=0
rgb_blink: cycle 3 r=0 g=0 b=51
rgb_blink: cycle 4 r=51 g=51 b=51
rgb_blink: cycle 5 r=0 g=0 b=0
```

Every **5 s** with an accumulating deadline:

```text
rgb_blink: alive #1
rgb_blink: alive #2
```

These lines prove the shared math and the values sent to `rgbLedWrite`, not visible hue, brightness
or brownout behavior.

### `button_events`

Once:

```text
button_events: ready poll_ms=10
```

For each non-NONE result from the shared FSM (GPIO0 is active-low and is polled with `millis()`):

```text
button_events: button event PRESS
button_events: button event RELEASE
button_events: button event CLICK
button_events: button event DOUBLE_CLICK
button_events: button event LONG_PRESS
```

Every **5 s**:

```text
button_events: alive #1
```

An unattended capture must not invent button events. Physical button feel and gesture timing remain
human checks even if an event line is observed during a coordinator press.

## Core-3.x API notes

- RGB uses `rgbLedWrite(CB_PIN_RGB, r, g, b)` after `cb_rgb_math_dim`; deprecated `neopixelWrite` is
  forbidden.
- Backlight, when needed later, uses `ledcAttach(pin, 4000, 10)` and `ledcWrite` with duty at most
  1023. Legacy `ledcSetup`/`ledcAttachPin` are forbidden.
- I2C, if an IMU sketch is ever reviewed in, starts with
  `Wire.begin(CB_PIN_I2C_SDA, CB_PIN_I2C_SCL, CB_I2C_FREQ_HZ)` (SDA 48 before SCL 47). The item-4
  CTRL1 auto-increment and CTRL9 initialization are mandatory; no raw-read-only IMU sketch is added
  in item 7.
- Battery arithmetic and QMI burst decoding remain in the staged pure helpers. There is no C++
  wrapper or duplicate driver implementation.
- SD examples must set the frozen pins and never format on failure. No item-7 sketch uses SD or the
  Arduino default partition as evidence of IDF FAT parity.

## Checklist 11 evidence boundary

The coordinator may record a serial-half PASS after one sketch compiles, uploads over Type-C and
actually prints its contracted lines. Quote the upload exit and captured cycle/alive or event lines
with a date. The following stay **PENDING** unless separately observed: visible LED hue/brightness,
button feel and gesture timing, visual “behaves like its IDF twin” claims, behavior of any sketch not
flashed, and long-run stability. Pin-drift proves source identity, not behavioral identity. See
[verification.md](verification.md) checklist 11 for measured results.
