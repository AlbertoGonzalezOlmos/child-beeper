# 02_rgb_button — RGB LED + BOOT button diagnostic example

**Status: build-verified (item 3, `./cb build 02_rgb_button` green) and serial-verified
2026-11-23 (RGB init + the full five-cycle serial contract observed on hardware). LED
appearance and button gestures remain PENDING (needs human) — see
[docs/verification.md](../../docs/verification.md).**

## What it shows

Two optional BSP parts, no display needed:

- **`cb_rgb`** — WS2812 (GRB) on GPIO38 via RMT (10 MHz). `cb_rgb_set()` applies the shared
  `cb_rgb_math_dim()` peak-cap (`CB_RGB_DIM_CAP_PERCENT 20` → peak 51) internally, so the LED
  never sees more than 20 % intensity. The example dims the values itself first (idempotent)
  so the logged numbers are exactly what the LED receives.
- **`cb_button`** — BOOT button (GPIO0; strapping pin, input+pull-up only, never driven)
  through the frozen `cb_button_fsm` behind a 10 ms `esp_timer` poller. Events arrive in the
  **esp_timer task**; the callback only logs and sets a flag, the LED stepping stays in the
  `app_main` loop (the documented callback-context rule, demonstrated).

Colour cycle every 2 s: red, green, blue, white, off (5 steps, announced each), repeating
with a monotonic counter. A BOOT **CLICK advances the cycle immediately** (so a press is
serially visible as `event CLICK` followed by the next `cycle` line).

## Serial contract

```
I (…) rgb_button: ready poll_ms=10 cap_percent=20
I (…) rgb_button: cycle 1 r=51 g=0 b=0
I (…) rgb_button: cycle 2 r=0 g=51 b=0
I (…) rgb_button: cycle 3 r=0 g=0 b=51
I (…) rgb_button: cycle 4 r=51 g=51 b=51
I (…) rgb_button: cycle 5 r=0 g=0 b=0
I (…) rgb_button: event CLICK            (per BOOT press; needs a finger)
I (…) rgb_button: alive #1               (every 5 s)
W (…) rgb_button: cycle 6 failed: ESP_ERR_INVALID_STATE   (only if the LED path is down;
                                        gated R14b: one line per failure streak, silent
                                        until a cycle succeeds again)
```

The `r/g/b` values are post-dim ("as sent"): red `(255,0,0)` is capped to `(51,0,0)`, white to
`(51,51,51)`. Event tokens: `PRESS`, `RELEASE`, `CLICK`, `DOUBLE_CLICK`, `LONG_PRESS`
(`CLICK` is deferred ≈350 ms after release by the frozen FSM). The `ready` line prints as
soon as init returns — no artificial delay; if early lines are lost to USB re-enumeration,
the repeating `cycle`/`alive` lines carry the contract. Driver-level lines from
`cb_rgb:`/`cb_button:`/`cb_bsp:` may appear above these and are not part of this contract.

| Symptom | Cause |
|---|---|
| LED dark, `cycle N r=…` lines OK | visibility/brightness of the 20 % cap through the acrylic (human check) |
| red/green visibly swapped etc. | colour-element order (GRB wiring) — human check |
| `cycle N failed: <err>` on the first failing step | RMT/led_strip path down; the error name (`ESP_ERR_INVALID_STATE` = not initialised) narrows it. Gated (R14b): one WARN per failure streak — later failures log nothing until a cycle succeeds again, so a dead LED shows one `failed` line and then only silent cycle numbers |
| no `event` lines ever | button part down (see `cb_bsp`/`cb_button` WARNs) or nobody pressed — gestures are a human check |
| `event CLICK` feels late/laggy | FSM defers CLICK ≈350 ms by design; timing is a human check |

## Build / flash

```bash
./cb build 02_rgb_button                # via the CLI
# raw (project owner only — writes ./sdkconfig):
idf.py -C examples/02_rgb_button -B /tmp/02_rgb_button build

./cb flash --target 02_rgb_button       # with a board attached; exits 2 with a
                                        # BOOT+RESET recovery hint when no port is found
./cb monitor
```
