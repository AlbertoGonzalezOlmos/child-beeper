# 04_imu — QMI8658 IMU diagnostic example

**Status: build-verified (item 4, `./cb build 04_imu` green); serial-half hardware-captured
2026-11-23 — see [docs/verification.md](../../docs/verification.md) checklist 8. The R23
strict resting acceptance, motion response and accuracy/bias calibration remain PENDING
(needs human).**

## What it shows

The optional **`cb_imu`** part (QMI8658 on the shared I2C bus, probed at **0x6A** then
**0x6B**) through `cb_bsp`: one `cb_imu_read()` every 2 s prints the whole
`cb_imu_sample_t` — acceleration in g, angular rate in degrees/second and die temperature
in °C, decoded from the frozen 14-byte `TEMP_L..GZ_H` burst. The device runs at the fixed
item-4 ranges **±8 g / ±512 dps** (no runtime range switching); the ODR comes from the
cb_bsp Kconfig choice `CB_IMU_ODR` (default **120 Hz**).

The `ready` line carries the probe evidence the hardware checklist greps for: which of the
two candidate addresses answered and the WHO_AM_I byte (expected `0x05`).

## Serial contract

```
I (…) imu: ready addr=0x6B who=0x05                                        (once)
I (…) imu: ax=+0.001 ay=-0.003 az=+1.002 gx=+0.06 gy=-0.12 gz=+0.00 temp_c=+25.31   (every 2 s)
I (…) imu: ax=-0.002 ay=+0.000 az=+0.998 gx=-0.03 gy=+0.05 gz=+0.01 temp_c=+25.32
I (…) imu: alive #1                                                        (every 5 s)
W (…) imu: read failed: ESP_ERR_TIMEOUT     (R13b: one-shot, then polling stops)
W (…) imu: no IMU found (probed 0x6A/0x6B), samples skipped  (once, when the part could
                                                        not be brought up: no ACK, WHO
                                                        mismatch, or config failure)
```

The `ready` line prints as soon as init returns — no artificial delay; if early lines are
lost to USB re-enumeration, the repeating sample/`alive` lines carry the contract.
`alive` keeps printing even after polling stopped, so a dead IMU is distinguishable from a
wedged app. The probe/WHO WARNs and the `i2c`/`cb_imu` init lines are owned by the driver
(not this example):

```
I (…) i2c: internal pull-ups enabled          (R12: hardware-pending electrical detail)
I (…) cb_imu: up addr=0x6B who=0x05 range=8g/512dps odr=120Hz
W (…) cb_imu: no qmi8658 at 0x6A or 0x6B      (when no device answers)
```

Part failures are logged by `cb_bsp` (`W (…) cb_bsp: IMU: init failed (optional,
suppressed): …`); the example never aborts on missing hardware.

## Resting acceptance (printed values, R23)

For a board resting flat on a desk, the serial sample lines should show:

| Field | Acceptance |
|---|---|
| largest `|ax|`/`|ay|`/`|az|` | **0.9 – 1.1** (≈ 1 g on the resting axis) |
| the other two accel axes | **< 0.15** each |
| `|gx|`, `|gy|`, `|gz|` | **< 2.00** dps each |
| `temp_c` | **10 – 50** °C |

These are sanity thresholds on serial numbers, **not** calibration or accuracy proof;
full-scale accuracy, axis orientation and motion response stay PENDING (human) — see
[docs/verification.md](../../docs/verification.md).

| Symptom | Cause |
|---|---|
| `no IMU found (probed 0x6A/0x6B)` | no device ACKed — wiring/power, or the R12 internal pull-ups are too weak at 400 kHz (hardware check) |
| `W cb_imu: who_am_i 0xNN != 0x05`, no `ready` line | unexpected WHO_AM_I revision — recorded verbatim in the WARN; IMU init fails (`ESP_ERR_INVALID_RESPONSE`), so the no-IMU line prints and samples are skipped |
| samples far from ≈1 g at rest | board tilted/moving, or scale misconfiguration — compare against the acceptance table |
| gyro ≫ 2 dps at rest | vibration, or the part is still settling after motion. The 2026-11-23 board shows ≈2–3 dps resting zero-rate bias (recorded in [docs/verification.md](../../docs/verification.md) checklist 8), so a healthy unit can exceed the strict <2.00 dps threshold |
| `read failed: …` then samples stop | the I2C read path is down (the one-shot WARN carries the error name); `alive` keeps the loop provably running |
| `temp_c` outside 10–50 | die very hot/cold, or a decode/burst problem — compare raw burst against `cb_qmi8658_regs.h` |

## Build / flash

```bash
./cb build 04_imu                   # via the CLI
# raw (project owner only — writes ./sdkconfig):
idf.py -C examples/04_imu -B /tmp/04_imu build

./cb flash --target 04_imu          # with a board attached; exits 2 with a
                                    # BOOT+RESET recovery hint when no port is found
./cb monitor
```
