# 03_battery — battery ADC diagnostic example

**Status: build-verified (item 3, `./cb build 03_battery` green) and serial-verified
2026-11-23 (curve-fitting calibration + stable battery samples observed on hardware).
Multimeter accuracy (±5 %) remains PENDING (needs human) — see
[docs/verification.md](../../docs/verification.md).**

## What it shows

The optional **`cb_battery`** part (ADC1_CH0 oneshot, GPIO1, 12 dB atten) through `cb_bsp`:
one `cb_battery_read()` every 2 s prints the whole `cb_battery_sample_t` — raw sample,
calibrated ADC-pin millivolts, battery millivolts after the frozen ×3 divider and the
700/695 eFuse gain, whether curve-fitting calibration was usable, and the plausibility flag.

`plausible` is a **warning heuristic** (inclusive 2500–4500 mV window), not cell detection
and not a state-of-charge estimate: a floating no-cell input can land inside the window.

## Serial contract

```
I (…) battery: ready
I (…) battery: raw=2587 pin_mv=1333 battery_mv=4027 calibrated=1 plausible=1   (every 2 s)
I (…) battery: raw=2587 pin_mv=1333 battery_mv=4027 calibrated=1 plausible=1
I (…) battery: alive #1                                                        (every 5 s)
W (…) battery: read failed: ESP_ERR_TIMEOUT     (R13b: one-shot, then polling stops)
```

The `ready` line prints as soon as init returns — no artificial delay; if early lines are
lost to USB re-enumeration, the repeating sample/`alive` lines carry the contract. The
transition-gated no-cell WARN is owned by the driver (not this example):

```
W (…) cb_battery: implausible 987 mV (no cell inserted?)
I (…) cb_battery: reading plausible again
```

Part failures are logged by `cb_bsp` (`W (…) cb_bsp: BATTERY: init failed (optional,
suppressed): …`); on a read failure the example logs `battery: read failed: %s` **once**
(R13b, frozen text) and then stops polling — no WARN spam — while the `battery: alive`
heartbeat continues.

| Symptom | Cause |
|---|---|
| `plausible=0` with a cell fitted | cell sag, contact resistance, or a bad plausibility guess — multimeter check (human) |
| `plausible=1` with no cell | floating input landed inside the heuristic window — expected limitation |
| `calibrated=0` | eFuse curve-fitting unavailable on this chip; linear `cb_math` fallback is in use (serial fact) |
| `battery_mv` off by a few % | divider/gain tolerances — only a multimeter establishes ±5 % accuracy (human) |
| samples stop after `read failed: …` | the ADC read path is down (the one-shot WARN carries the error name); alive keeps the loop provably running |

## Build / flash

```bash
./cb build 03_battery                   # via the CLI
# raw (project owner only — writes ./sdkconfig):
idf.py -C examples/03_battery -B /tmp/03_battery build

./cb flash --target 03_battery          # with a board attached; exits 2 with a
                                        # BOOT+RESET recovery hint when no port is found
./cb monitor
```
