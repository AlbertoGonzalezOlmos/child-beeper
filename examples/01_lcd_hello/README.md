# 01_lcd_hello — display-stack example

**Status: build-verified only (item 1 skeleton). Nothing here is hardware-verified — see
[docs/verification.md](../../docs/verification.md).**

## What it proves now (item 1)

- The canonical example-project pattern: two `SDKCONFIG_DEFAULTS` entries, the manifest-relative
  (three-ups) `cb_bsp` path dependency, custom partition table picked up from
  `config/partitions.csv`.
- The BSP path-dependency wiring compiles and links (`PRIV_REQUIRES cb_bsp`).
- Expected serial output while it is a skeleton: `cb_bsp` warnings for the not-yet-implemented
  parts, then `cb_bsp_init(BACKLIGHT|LCD|LVGL) -> ESP_ERR_NOT_SUPPORTED` and a heartbeat —
  the app never aborts.

## What item 2 adds

The diagnostic screen: colour bars plus one-pixel markers at all four edges of the 172x320 panel.
On hardware day that screen is the x-gap (34), colour-order and inversion check: bars must reach
all four edges and the markers must sit exactly in the corners.

## Build / flash

```bash
./cb build 01_lcd_hello                 # build via the CLI
# or, raw, from inside this directory (project owner only — writes ./sdkconfig):
idf.py -B /tmp/01_lcd_hello build

./cb flash --target 01_lcd_hello        # with a board attached; exits 2 with a
                                        # BOOT+RESET recovery hint when no port is found
./cb monitor
```
