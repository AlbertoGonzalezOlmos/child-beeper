# 06_dashboard — self-test firmware + LVGL dashboard (toolkit item 6)

Standalone ESP-IDF project (canonical example pattern) that initializes **all** of `cb_bsp`
(`cb_bsp_init(CB_PART_ALL)`), collects one piece of evidence per peripheral, prints the frozen
seven-item **selftest report** on the console, and then runs a labels-only LVGL 9.5 dashboard with
live battery/IMU values and a 5 s `alive` heartbeat. It is the firmware behind `./cb selftest`
(glm53's strict ST2 parser validates exactly these lines).

**Status:** build-verified (`./cb build 06_dashboard`, IDF v6.1, 06_dashboard.bin 685168 bytes).
Hardware-pending — the serial capture is `docs/verification.md` checklist 10; nothing here is
claimed hardware-verified until that capture exists.

## What the screen shows (172×320 ST7789, labels only)

Title `Child Beeper` + `selftest cb_bsp=<ver>`, seven status rows in the serial order
**lifecycle, lcd, rgb, button, battery, imu, sd** (PASS green, FAIL red, SKIP amber), a live
battery row (`bat=<mV>mV plausible=<0|1>`), a live IMU row (`imu_mag=<g>g addr=0x<AA>`) and an
`alive #N` tick. Live values refresh every **2 s** from the same `cb_battery_sample_t` /
`cb_imu_sample_t` structs the serial lines print. No images, no canvas, no PSRAM; every `lv_*` call
runs under `cb_lcd_lvgl_lock()` with a finite timeout — a lock timeout skips that UI update (gated
`W (…) dashboard: lvgl lock timeout`) and never touches serial output.

## Build / flash / monitor

```bash
. ~/.espressif/tools/activate_idf_v6.1.sh
./cb build 06_dashboard        # or: idf.py -B build/06_dashboard build   (inside this dir)
./cb selftest                  # build + flash + capture + validate the report (needs a board)
./cb monitor
```

## Serial contract (frozen strings — ITEM6-RULINGS gold table)

The selftest block is printed **bare** (no `I (…) dashboard: ` prefix) back-to-back between
`SELFTEST BEGIN` and `RESULT:`, so the wire lines are byte-identical to the gold table. Exactly
seven ITEM lines in the frozen order, then exactly one RESULT line; live lines start after RESULT.
PASS/FAIL items carry `key=value` evidence; every SKIP carries `reason=` as its **first** field
(`needs-human` = button only, `no-card` = sd only, `absent` = init failure that proves absence).

Golden bench report (no TF card, no finger — sums to 7):

```
SELFTEST BEGIN version=1 cb_bsp=0.5.0
ITEM:lifecycle PASS init=ESP_OK
ITEM:lcd PASS lvgl=9.5.0 frames=1
ITEM:rgb PASS cap_percent=20
ITEM:button SKIP reason=needs-human poller=running
ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1
ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99
ITEM:sd SKIP reason=no-card
RESULT:PASS total=7 pass=5 fail=0 skip=2
```

With a mounted TF card the `ITEM:sd` line becomes `ITEM:sd PASS mounted=1 width=4` and the tally
becomes `RESULT:PASS total=7 pass=6 fail=0 skip=1`. `RESULT:PASS` iff `fail==0`; the CLI lists
failing items on `RESULT:FAIL`. Verdict variants (each ITEM line is one of:):

| Item | PASS | FAIL | SKIP |
|---|---|---|---|
| `lifecycle` | `init=ESP_OK` | `init=<esp_err_to_name>` | — |
| `lcd` | `lvgl=9.5.0 frames=1` | `lvgl=9.5.0 frames=0` | — |
| `rgb` | `cap_percent=20` | `init=<esp_err_to_name>` | — |
| `button` | — | — | `reason=needs-human poller=running` (always; frozen) |
| `battery` | `battery_mv=<mV> pin_mv=<mV> calibrated=<0\|1> plausible=<0\|1>` | same fields with `plausible=0`, or `init=<err>` / `read=<err>` when no sample exists | — |
| `imu` | `addr=0x<aa> who=0x05 mag_g=<g>` | same fields with `mag_g` outside 0.50–1.50 g, or `… read=<err>`, or `init=<err>` | `reason=absent` |
| `sd` | `mounted=1 width=<n>` | `mounted=1 width=0` | `reason=no-card` |

`frames=1` is the example's own observed LVGL refresh-ready event for its first frame (not pixel
proof). `mag_g` is one resting magnitude sample in g (window 0.50–1.50 g).

After `RESULT:`, bare live lines every **2 s** and an ESP_LOGI heartbeat every **~5 s**
(accumulating deadline, first at ~5 s):

```
live bat=4221mV plausible=1 imu_mag=0.99g addr=0x6B
live bat=NAmV plausible=0 imu_mag=NAg addr=0x00
I (…) dashboard: alive #1
```

Unavailable sources use the NA forms `bat=NAmV` / `imu_mag=NAg` / `addr=0x00` — a stale value is
never presented as current. Gated WARNs (one-shot per failing run, re-armed on success):
`W (…) dashboard: lvgl lock timeout`, `W (…) dashboard: battery read failed: %s`,
`W (…) dashboard: imu read failed: %s` (`%s` = `esp_err_to_name()`).

`./cb selftest` (tools/cb.py) builds and flashes this project, captures until `RESULT:`, validates
the grammar strictly and prints `[PASS] selftest RESULT:PASS total=7 pass=5 fail=0 skip=2` plus
`cb: 2 skipped (unverified)` — SKIP is never hardware verification.

## Honest claims (ST6)

| Item | Proves | Does not prove |
|---|---|---|
| `lifecycle` | `cb_bsp_init(CB_PART_ALL)` return | optional-part health |
| `lcd` | LVGL up + first frame observed flushed (`frames=1`) | pixel correctness, colours, legibility, tearing |
| `rgb` | driver init + frozen 20% cap | visible brightness or colour |
| `button` | always SKIP `needs-human` | any gesture — human verification required |
| `battery` | one plausible calibrated sample | multimeter accuracy or physical cell presence |
| `imu` | answering address + WHO 0x05 + resting magnitude | motion response, orientation, accuracy |
| `sd` | mounted + bus width (needs a card) | mounted I/O; `no-card` is not slot-empty proof (no CD pin) |

The frozen `poller=running` field on the button line is a contract constant (ITEM6-RULINGS), not an
observation; the button item is always unverified.

## Coordinator greps (checklist 10 capture)

```bash
grep -c '^SELFTEST BEGIN version=1 cb_bsp=0\.5\.0'    # 1
grep -c '^ITEM:'                                      # 7
grep -E '^ITEM:button SKIP reason=needs-human poller=running$'   # exactly this line
grep -E '^RESULT:PASS total=7 pass=5 fail=0 skip=2'   # no-card bench (pass=6 skip=1 with card)
grep -E '^live bat=[0-9]+mV plausible=[01] imu_mag=[0-9.]+g addr=0x[0-9A-F]{2}'
grep -c 'dashboard: alive #'                          # keeps increasing (~every 5 s)
```
