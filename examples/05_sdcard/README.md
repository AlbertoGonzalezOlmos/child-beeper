# 05_sdcard — TF card over SDMMC (toolkit item 5)

Standalone ESP-IDF project (canonical example pattern) that exercises the optional
**cb_sd** part: an SDMMC slot-1 connection to a micro-SD (TF) card in 4-bit mode with
the driver's logged 1-bit fallback, FAT mounted at the frozen `/sdcard` mount point.
The example runs a 4 KB write → read → verify → delete round-trip three times, unmounts
cleanly and keeps a 5 s `alive` heartbeat forever — including in every degraded mode.

It exists as a copy-paste template and as the hardware evidence source for the SD
peripheral (docs/verification.md checklist 9 greps this example's serial).

**Init path (deliberate):** the example drives the driver directly — one
`cb_sd_init()` call, exactly **one** mount attempt — so it owns the mount error
code printed in the frozen `no card or mount failed: %s` line. The BSP-wide init
path (`cb_bsp_init`, optional-part suppression) is exercised by the root app,
not here.

## Wiring (frozen in `components/cb_bsp/include/cb_pins.h`)

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| CLK    | 14   | | D1     | 18   |
| CMD    | 15   | | D2     | 17   |
| D0     | 16   | | D3     | 21   |

SDMMC slot 1, 4-bit bus, internal pull-ups; bus frequency is the one cb_bsp Kconfig
knob `CONFIG_CB_SD_MAX_FREQ_KHZ` (default 20000 kHz). No card-detect line exists.

## Build / flash / monitor

```bash
. ~/.espressif/tools/activate_idf_v6.1.sh
./cb build 05_sdcard        # or: idf.py -B build/05_sdcard build   (inside this dir)
./cb flash                  # checks the serial port first
./cb monitor
```

## Serial contract (frozen strings — R23/SD10)

Every line this example can ever print (`sdcard` tag). Byte-exact; do not paraphrase.

| Line | When |
|---|---|
| `I (…) sdcard: ready width=%d mount=%s total_bytes=%llu free_bytes=%llu` | once, mounted and `cb_sd_info` OK. `width` = negotiated bus width (4 or 1), `mount` = `/sdcard`, numbers are **bytes** |
| `I (…) sdcard: roundtrip ok path=%s bytes=4096 verified=1 cleanup=deleted` | per round — printed only after the verify matched AND the file was deleted again |
| `W (…) sdcard: no card or mount failed: %s` | once when not mounted (then heartbeat only); `%s` = `esp_err_to_name()` of the mount error |
| `W (…) sdcard: roundtrip failed: %s` | one-shot for any round-trip failure (then polling stops); cause text below |
| `I (…) sdcard: alive #%u` | first at ~5 s, then every ~5 s (accumulating deadline), forever |

Cause text for `roundtrip failed: %s`: `strerror(errno)` for I/O failures
(create/write/fsync/close/read/unlink), `short read` / `verify mismatch at offset %u`
for compare failures, `esp_err_to_name()` if `cb_sd_info` itself fails (the phase's
setup — capacity is required evidence — then stops).

Driver-owned SD lines (cb_sd, tag `cb_sd`) the checklist also greps — see
`docs/api.md` for the full contract:

```
I (…) cb_sd: up mount=/sdcard width=%d max_freq_khz=%d
W (…) cb_sd: 4-bit mount failed: %s — retrying 1-bit
W (…) cb_sd: mount failed: %s (no card?)
I (…) cb_sd: already initialized
I (…) cb_sd: unmounted
W (…) cb_sd: unmount failed: %s
```

## Round-trip protocol (SD11)

- Unique path `/sdcard/cb05_rt%03u.tmp`, counter from `000`, bumped on `EEXIST`
  up to `099`; opened with `O_CREAT | O_EXCL | O_WRONLY` — a stale or foreign file
  is never clobbered, truncated, renamed or reused.
- Content: 4096 bytes of the deterministic pattern `(uint8_t)(i * 31 + 7)`.
- Read back and compared byte-for-byte; then `unlink()` of **exactly that path**.
- 3 rounds, 10 s apart (first one immediately), then `cb_sd_unmount()` once.
  Any failure prints the one-shot `roundtrip failed: %s` and stops polling;
  the `alive` heartbeat never stops.

**Cleanup guarantee:** the example deletes only the exact path it created this boot
(the `O_EXCL` create is the ownership proof) and nothing else; `cleanup=deleted` is
reported only when that unlink succeeded. Failure-path cleanup is a best-effort
silent unlink (a failing round leaks at most its own file — a leftover `cb05_rt*.tmp`
is an observable anomaly and would surface later as an `EEXIST` skip). FAT metadata
and free space still change — no bit-for-bit card preservation is claimed.
The example has no code that could format anything (the driver's one-mount-site +
`format_if_mount_failed = false` invariant is documented in `docs/api.md`).

## Honest claims (SD12)

We do not know whether a TF card is inserted on the bench (there is no card-detect
line). The no-card path is hardware-observable; the mounted path, bus width and
1-bit fallback stay **PENDING** unless the coordinator's flash shows `ready`.
**A mount error is not proof that no card exists** — record "mount failed" unless
the slot was visually confirmed empty.

## Coordinator greps (anchored — `) sdcard:` / `) cb_sd:`)

Scenario A (card with a mountable FAT12/16/32):
```bash
grep -cE '\) sdcard: ready width=[14] mount=/sdcard total_bytes=[0-9]+ free_bytes=[0-9]+'   # 1
grep -cE '\) sdcard: roundtrip ok path=/sdcard/cb05_rt[0-9]{3}\.tmp bytes=4096 verified=1 cleanup=deleted'   # 3
grep -cE '\) cb_sd: up mount=/sdcard width=[14] max_freq_khz=[0-9]+'                        # 1
grep -cE '\) cb_sd: unmounted'                                                              # 1
grep -E  '\) sdcard: alive #'                 # keeps increasing after the rounds
grep -E  '\) sdcard: (roundtrip failed|no card)'                                            # EMPTY
```
A `W (…) cb_sd: 4-bit mount failed: … — retrying 1-bit` before `up … width=1` is a
PASS for the 1-bit fallback path — record it verbatim.

Scenario B (no card / unmountable card) — exactly ONE mount attempt:
```bash
grep -cE '\) sdcard: no card or mount failed: '                                             # 1
grep -cE '\) sdcard: (ready|roundtrip)'                                                     # EMPTY
grep -cE '\) cb_sd: 4-bit mount failed: '                                                   # 1
grep -cE '\) cb_sd: mount failed: '                                                         # 1
grep -E  '\) sdcard: alive #'                 # keeps increasing
```

## Status

- **Build-verified** (gate green): API frozen, example builds against cb_bsp 0.4.0.
- **Hardware-pending** (docs/verification.md checklist 9): everything card-related —
  mount success, bus width, capacities, round-trips, unmount, 1-bit fallback, and
  the no-card degrade path until a coordinator capture exists.
