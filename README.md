# child-beeper

Firmware toolkit for the [Waveshare ESP32-S3-LCD-1.47B](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B)
(ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, 172x320 ST7789 LCD, no touch, QMI8658 IMU, one
addressable RGB LED, BOOT button, battery ADC, TF slot, native USB-Serial/JTAG): a reusable board
support package (`components/cb_bsp`) plus one example per peripheral, and an Arduino path whose
umbrella-only CBBoard library stages the same canonical pins/pure helpers at build time (item 7).

The root app is a **Wi-Fi messenger, clock and daily visual alarm**. Incoming messages
appear on the LCD while the side light pulses blue/yellow. Use BOOT for preset replies
and alarm setup, or the [Android companion app](android/README.md) to type messages,
sync the clock and manage alarms. See [the messaging guide](docs/messaging.md).

## Quickstart (board optional)

```bash
./cb doctor     # toolchain check: ESP-IDF v6.1, arduino-cli, cores, component caches
./cb verify     # host tests + builds + audits — the gate
./cb arduino    # stage CBBoard, then compile rgb_blink + button_events (no board needed)
```

## With a board attached

```bash
./flash.sh       # one command: build the app (plus its Wi-Fi settings image) and flash it
./flash.sh --monitor   # also open the serial console afterwards (Ctrl-] quits)
./cb flash       # refuses with exit 2 + BOOT/RESET recovery hint when no port is found
./cb monitor
./cb arduino rgb_blink --upload   # one sketch only; no port => exit 2 + BOOT+RESET hint
```

## Wi-Fi

The root app connects to the network named in **`wifi_settings.txt`** (repo root; created from
`wifi_settings.example.txt` on the first `./flash.sh` run). The build embeds the file into the FAT
`storage` partition and flashes it with the app, so the flow is: edit `wifi_settings.txt` →
`./flash.sh`. With no SSID configured, Wi-Fi stays off; initialization failures never stop
the app. The serial status shows `wifi=off` / `wifi=connecting` / `wifi=connected <ip>` / `wifi=failed`.
The worker reconnects automatically after network loss. The file is gitignored, so real
passwords never land in git; local FAT build images contain them and should stay private.

See [docs/index.md](docs/index.md) for the full documentation map (hardware, ESP-IDF, Arduino,
flashing, API, verification, troubleshooting) and [AGENTS.md](AGENTS.md) for working rules.

What is **build-verified** vs **hardware-verified** is tracked per peripheral in
[docs/verification.md](docs/verification.md) — check it before trusting a feature; the hardware-day
checklist there records what has actually been observed on the board.

## Layout

`main/` messenger/clock/alarm app · `android/` companion app · `components/cb_bsp/` the BSP · `examples/` per-peripheral
firmware (`examples/01_lcd_hello` is the canonical project pattern) · `config/` shared board
defaults + partition table · `tests/host/` host-compiled unit tests · `tools/` the `./cb` CLI ·
`docs/` · `arduino/` CBBoard umbrella/manifest and the `rgb_blink` / `button_events` sketches
(shared `cb_*` files are staged into the build root, not committed twice).
