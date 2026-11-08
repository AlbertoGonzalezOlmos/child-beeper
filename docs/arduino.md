# Arduino path — arduino-cli + Arduino IDE

> Status: toolchain facts build-verified on this machine (arduino-cli 1.4.1, core `esp32:esp32`
> 3.3.12 installed and compiling offline). The `arduino/` tree (CBBoard library + sketches) lands
> in **item 7** — until then `./cb arduino` reports "no sketches yet", and the `./cb verify` steps
> `pin-drift` and `arduino:*` legitimately **SKIP**. Nothing hardware-verified — see
> [verification.md](verification.md).

## Pinned versions

| Item | Value |
|---|---|
| arduino-cli | **1.4.1** (`~/.local/bin/arduino-cli`) |
| Core | **`esp32:esp32` 3.3.12** — *not* the old `arduino:esp32` 2.0.18 core also present on this machine |
| Arduino IDE | `/usr/bin/arduino` (optional; CLI is the gate) |
| Libraries | none required for the core sketches (item 7); tier-2 LVGL needs `lvgl@9.5.0` (network install once) |

## The locked FQBN

```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc,PartitionScheme=default
```

Option by option (all verified present in core 3.3.12 `boards.txt`):

| Option | Value | Why |
|---|---|---|
| `PSRAM` | `opi` | 8 MB **octal** PSRAM — anything else and PSRAM is unusable |
| `FlashSize` | `16M` | 16 MB flash |
| `CDCOnBoot` | `cdc` | `Serial` on the Type-C port (matches IDF's USB-Serial/JTAG console) |
| `USBMode` | `hwcdc` | Hardware CDC and JTAG (native USB, no TinyUSB) |
| `PartitionScheme` | `default` | the measured-working scheme; `app3M_fat9M_16MB` ("3MB APP/9.9MB FATFS") is documented as the size-relief option if a sketch ever outgrows it |

Arduino IDE menu equivalents (Board: **ESP32S3 Dev Module**): PSRAM → *OPI PSRAM*; Flash Size →
*16MB (128Mb)*; USB CDC On Boot → *Enabled*; USB Mode → *Hardware CDC and JTAG*; Partition Scheme
→ *Default* (or *3MB APP/9.9MB FATFS* for the relief option).

## Build commands

Through the toolkit (what `./cb verify` runs; item 7):

```bash
./cb arduino                 # build all sketches
./cb arduino <sketch>        # build one
./cb arduino --install-libs  # tier-2: installs lvgl@9.5.0 (network; never implicit)
./cb arduino-install         # export the staged CBBoard library into the IDE sketchbook
```

Raw equivalent (learners / IDE):

```bash
arduino-cli compile \
  -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc,PartitionScheme=default" \
  --library <staged-CBBoard-dir> --build-path build/arduino/<sketch> --warnings all \
  arduino/sketches/<sketch>
```

Uploads use the same FQBN plus `-p /dev/ttyACM0` (or the IDE's port menu); the same no-board guard
and BOOT+RESET recovery apply — see [flashing.md](flashing.md).

## Library staging (how the pin map stays single-source)

- The shared headers live **once** in `components/cb_bsp/include/` (`cb_pins.h`, `cb_math.h`,
  `cb_button_fsm.h`, `cb_qmi8658_regs.h` — pure, portable C).
- Before every Arduino build, the tooling **stages** a self-contained copy of
  `arduino/libraries/CBBoard` into the build tree, copying the shared headers in. Staged copies are
  never committed and never hand-edited.
- `./cb verify` step `pin-drift` byte-compares the Arduino copies against the canonical headers
  (SKIP while `arduino/` is absent).
- Item-7 note: staging must also provide the **implementations** (`cb_math.c`,
  `cb_button_fsm.c` — both compile as C and C++) whenever Arduino code calls them; header-only
  staging would compile but fail at link time.

## Core-3.x API notes for sketches (frozen choices)

- RGB LED: **`rgbLedWrite(CB_PIN_RGB, r, g, b)`** (core builtin). `neopixelWrite` is deprecated in
  3.3.12 and on the forbidden-construct list — do not use it. Keep brightness low (the LED sits
  behind acrylic).
- Backlight: `ledcAttach(pin, 4000, 10)` + `ledcWrite(pin, duty)` with duty **≤ 1023** at 10-bit
  (the vendor demo writes 1024 = off-by-one; see
  [troubleshooting.md](troubleshooting.md#vendor-bug-ledger)). The removed 2.x-style
  `ledcSetup(`/`ledcAttachPin(` are forbidden constructs.
- I2C/IMU: `Wire.begin(CB_PIN_I2C_SDA, CB_PIN_I2C_SCL, CB_I2C_FREQ_HZ)` — **SDA first** (48, 47);
  the ESP32-S3 default Wire pins are wrong for this board. Probe both 0x6A and 0x6B.
- Battery: `analogReadMilliVolts(CB_PIN_BAT_ADC)`, then apply the frozen conversion
  `V = mv × 3/1 × 700/695 / 1000` (divider/gain macros live in `cb_pins.h`).
- SD card: `SD_MMC.setPins(14, 15, 16, 18, 17, 21)` before `SD_MMC.begin(...)` — argument order is
  CLK, CMD, D0, D1, D2, D3 — and **never** pass format-on-fail `true`.
- Button: GPIO0 active-low; feed the shared `cb_button_fsm` from `loop()` with `millis()`.

## Tier-2 (optional, never gates item 1)

An LVGL 9 sketch requires `arduino-cli lib install lvgl@9.5.0` (network once) and an
`lv_conf.h` placement verified by compile probe. The vendor's Arduino LVGL glue is **LVGL 8** API
(`lv_disp_drv_t`, `lv_scr_act(`) and must not be reused — rewrite against LVGL 9 if/when this
sketch is built.
