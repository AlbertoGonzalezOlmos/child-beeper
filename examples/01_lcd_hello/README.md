# 01_lcd_hello — display-stack diagnostic example

**Status: build-verified (item 2); init + serial output hardware-verified — colours,
geometry and sustained rendering are pending the operator's visual check (see
[docs/verification.md](../../docs/verification.md) for the recorded results and the
remaining hardware-day checklist).**

## What it shows

The full display stack through the BSP: backlight (LEDC, raised by cb_lcd's one-shot
first-frame hook), the ST7789 panel over SPI2 @ 40 MHz with gap(34,0) + inversion, and
LVGL 9.5 via esp_lvgl_port. The port requires the panel's **io handle** (not just the
panel handle) to register its flush-ready callback, and its two draw buffers are
**internal DMA-capable** (2 x 172x40 px x 2 B = 27,520 B of internal SRAM) — esp_lvgl_port
rejects `buff_dma && buff_spiram` on this SoC. PSRAM instead holds LVGL's 64 KB heap and
this example's 110 KB pattern canvas.

On screen: kimi's diagnostic pattern — 8 vertical colour bars (white, yellow, cyan,
green, magenta, red, blue, black), a full-width grey ramp along the bottom, 2x2 markers
in all four corners — plus a label with the resolution and a frame counter that updates
every 2 s. On hardware this pattern separates the classic failure axes:

| Symptom | Cause |
|---|---|
| bars shifted / right edge cropped | x-gap wrong (must be 34) |
| red and blue bars swapped | RGB/BGR element order |
| black bar glows, white bar dull | inversion wrong |
| corners cut or mirrored | gap/rotation/mirror config |

## Serial contract

```
I (…) lcd_hello: display: 172x320 ST7789, spi2 @40MHz, gap(34,0), lvgl 9.5
I (…) lcd_hello: frame 1
I (…) lcd_hello: frame 2        (every 2 s)
```

If the display stack fails to init, the error is logged once and the heartbeat continues
(serial stays usable for debugging with a dead panel).

## Build / flash

```bash
./cb build 01_lcd_hello                 # via the CLI
# raw (project owner only — writes ./sdkconfig):
idf.py -C examples/01_lcd_hello -B /tmp/01_lcd_hello build

./cb flash --target 01_lcd_hello        # with a board attached; exits 2 with a
                                        # BOOT+RESET recovery hint when no port is found
./cb monitor
```
