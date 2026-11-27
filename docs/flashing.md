# Flashing & monitor — native USB-Serial/JTAG

> Status (updated 2026-11-23): flashing and monitoring are **hardware-verified** on
> `/dev/ttyACM0` (`./cb flash --target app`, live `./cb monitor`, ~11 min heartbeat soak); the
> guard is verified via `CB_FORCE_NO_PORTS=1`; the **BOOT+RESET recovery drill is still pending**
> (needs a physical press). Checklist: [verification.md](verification.md).

## How the board connects

The Type-C port is the ESP32-S3's **native USB-Serial/JTAG** peripheral — there is no CH340/CP210x
bridge chip. Consequences:

- On Linux the board appears as **`/dev/ttyACM*`** (CDC-ACM), not `/dev/ttyUSB*`.
- On macOS use **`/dev/cu.usbmodem*`**. `./cb ports` discovers it automatically;
  `./cb flash --port /dev/cu.usbmodem1101` also works. Bluetooth/debug-console ports are excluded.
- The **same port** carries flashing, the log console, and (after reset) re-enumeration — the port
  may briefly disappear and reappear when the chip resets mid-flash; that is normal.
- Logs only appear on this port when the firmware selects it: IDF
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (already in `config/sdkconfig.board.defaults`); Arduino
  `CDCOnBoot=cdc` + `USBMode=hwcdc` (already in the locked FQBN, see [arduino.md](arduino.md)).
  The alternative console (UART0, GPIO43/44) is **not broken out** on this board.
- Permissions: the user must be in the `dialout` group (`sudo usermod -aG dialout $USER`, then
  re-login), or use a udev rule.

## Commands

```bash
./cb ports                 # list candidate ports (/dev/ttyACM*, /dev/ttyUSB*)
./cb ports --json          # machine-readable
./cb flash --target app            # guard → build → idf.py flash
./cb flash --target 01_lcd_hello --port /dev/ttyACM0
./cb monitor               # live stream; Ctrl-] quits
./cb monitor --port /dev/ttyACM0 --target 01_lcd_hello   # follow the FLASHED target: its build
                           # dir supplies the ELF for backtrace decoding + idf_monitor's image check
./cb selftest              # (item 6+) flash+run the self-test and parse its serial contract
```

Arduino uploads go through `./cb arduino` / the IDE with the same FQBN (item 7+).

### What you will (and will not) see on the console

- After every reset the native USB peripheral **re-enumerates** and host attachment is
  timing-dependent, so ROM/bootstrap/bootloader output and the app's early banner/WARN lines
  **may be dropped**. On 2026-11-23 the monitor captured the `I (683)` banner and `W (684…)`
  warnings, proving that early output is not categorically lost.
- The root messenger prints its **pairing code after two seconds**, once USB is observable,
  then logs Wi-Fi, clock, unread and alarm state every ten seconds. The peripheral examples
  retain their own diagnostic output.
- `./cb monitor` streams live (`cb: monitoring — press Ctrl-] to quit`; it warns when stdin is not
  a terminal, because then Ctrl-] may not reach the monitor). A raw `cat /dev/ttyACM0` shows the
  same stream (heartbeats every 2 s). `--target <T>` selects which build dir's ELF the monitor
  uses (default `app`).
- idf_monitor's **"Checksum mismatch between flashed and built applications"** warning is
  **cosmetic**: the monitor's ELF is not byte-identical to the flashed image — expected when
  monitoring a different target than was flashed (pass `--target`) or after any rebuild (the app
  descriptor embeds build time/git SHA). The console stream and resets are unaffected.

## The no-board guard (exit code 2)

`flash`, `monitor` and `selftest` check ports **before** building or calling esptool. With nothing
attached (or with `CB_FORCE_NO_PORTS=1` set) they print a recovery hint and exit **2**:

```
cb: no usable serial port found (looked for /dev/ttyACM*, /dev/ttyUSB*, /dev/cu.usbmodem*, /dev/cu.usbserial*).
cb: the board is not connected, or the cable is charge-only.
  1. connect the board with a USB-C data cable
  2. if it does not enumerate: hold BOOT, tap RESET, release BOOT (download mode)
  3. Linux: check `dmesg | tail` and the `dialout` group; macOS: check System Information > USB
  4. then: ./cb ports
```

If **more than one** port exists, the tools refuse and list the candidates — they never guess;
pass `--port`. The guard logic is provable without hardware via `CB_FORCE_NO_PORTS=1`
(`./cb verify` step `guards-no-board` does exactly this and asserts exit 2).

## Recovery: download mode (BOOT + RESET)

If flashing fails, the board boot-loops, or nothing enumerates:

1. Hold **BOOT** (GPIO0).
2. Press and release **RESET**.
3. Release **BOOT**.

The chip is now in download mode (GPIO0 is a strapping pin — see
[hardware.md](hardware.md#strapping-pin-warnings-read-before-using-gpio04546)); re-run
`./cb ports` and flash again. After flashing, tap RESET (or power-cycle) to run the new firmware.

## Troubleshooting quick pointers

- Port never appears: charge-only cable (use a data cable), missing `dialout`, or dead USB port —
  then download-mode recovery above. Full list in
  [troubleshooting.md](troubleshooting.md#flashing-and-serial).
- Firmware flashes but no logs: console not on USB-Serial/JTAG — check the sdkconfig key /
  FQBN options above.
- Never run `esptool erase_flash` casually; NVS and the FAT `storage` partition live on the same
  chip.
