# child-beeper

Firmware toolkit for the [Waveshare ESP32-S3-LCD-1.47B](https://www.waveshare.com/wiki/ESP32-S3-LCD-1.47B)
(ESP32-S3R8, 16 MB flash, 8 MB octal PSRAM, 172x320 ST7789 LCD, no touch, QMI8658 IMU, one
addressable RGB LED, BOOT button, battery ADC, TF slot, native USB-Serial/JTAG): a reusable board
support package (`components/cb_bsp`) plus one example per peripheral, with an Arduino path to come.

## Quickstart (board optional)

```bash
./cb doctor     # toolchain check: ESP-IDF v6.1, arduino-cli, cores, component caches
./cb verify     # host tests + builds + audits — the gate
```

## With a board attached

```bash
./cb flash      # refuses with exit 2 + BOOT/RESET recovery hint when no port is found
./cb monitor
```

See [docs/index.md](docs/index.md) for the full documentation map (hardware, ESP-IDF, Arduino,
flashing, API, verification, troubleshooting) and [AGENTS.md](AGENTS.md) for working rules.

What is **build-verified** vs **hardware-verified** is tracked per peripheral in
[docs/verification.md](docs/verification.md) — check it before trusting a feature; the hardware-day
checklist there records what has actually been observed on the board.

## Layout

`main/` root "hello beeper" app · `components/cb_bsp/` the BSP · `examples/` per-peripheral
firmware (`examples/01_lcd_hello` is the canonical project pattern) · `config/` shared board
defaults + partition table · `tests/host/` host-compiled unit tests · `tools/` the `./cb` CLI ·
`docs/`.
