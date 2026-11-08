# ESP-IDF path — environment, `./cb`, and raw `idf.py`

> Status: build-verified (item 1, 2026-11-08). Nothing hardware-verified — see
> [verification.md](verification.md).

## Toolchain (pinned)

| Item | Value |
|---|---|
| ESP-IDF | **v6.1** at `~/.espressif/v6.1/esp-idf` |
| Activate | `. ~/.espressif/tools/activate_idf_v6.1.sh` (per shell; `./cb` does this for you) |
| Compiler | `xtensa-esp-elf-gcc 15.2.0` |
| Target | `esp32s3` (comes from `config/sdkconfig.board.defaults`; no `-DIDF_TARGET` needed) |
| Components | `lvgl/lvgl 9.5.0`, `espressif/esp_lvgl_port 2.9.0`, `espressif/led_strip 3.1.1` (exact pins; `idf: ">=6.1.0,<6.2.0"`) |

`./cb doctor` checks all of the above and prints actionable errors; run it first on a new machine.

## `./cb` — the single entry point

`./cb` (repo root shim → `tools/cb.py`, Python 3 stdlib only). Frozen surface:

```
./cb doctor [--json] [--offline]     env/toolchain/board/disk checks
./cb deps [--offline]                resolve + warm the component cache, write dependencies.lock
./cb list                            app + examples discovered on disk
./cb build [TARGET...|--all]         TARGET = "app" | example dir name (e.g. 01_lcd_hello)
./cb test                            host unit tests (make -C tests/host test)
./cb verify [--fast] [--no-arduino] [--offline]
./cb ports [--json]
./cb flash [--port P] [--target T]
./cb monitor [--port P]
./cb selftest [--port P]
./cb arduino [SKETCH...] [--install-libs]
./cb arduino-install
./cb report                          show the verify report
./cb clean [TARGET|--all]
```

Exit codes: **0** ok · **1** build/test failure · **2** board/port absent (with BOOT+RESET recovery
hint) · **3** usage/environment problem. In `verify`: a FAIL in `doctor` → 3, any other FAIL → 1.

Conventions:
- **Build dirs**: `${CB_BUILD_ROOT:-build}/<target>`. When several people/agents share the tree,
  each exports their own root, e.g. `export CB_BUILD_ROOT=build/<your-name>`.
- **Concurrency**: `./cb` holds a **repo-wide** `flock` (`<repo>/.cache/cb-build.lock`,
  git-ignored) around every IDF/make/arduino-cli call — it serialises agents even when they use
  different `CB_BUILD_ROOT`s — and passes `-DSDKCONFIG=<build_root>/<target>/sdkconfig`, so
  parallel builds never share a project-dir `sdkconfig`.
- **Reports**: `${CB_BUILD_ROOT:-build}/verify-report.md|json`; host test binary at
  `${CB_BUILD_ROOT:-build}/host/run_tests`.
- Only `./cb deps` and `./cb arduino --install-libs` touch the network; nothing is installed
  implicitly.

Typical session:

```bash
./cb doctor          # environment green?
./cb deps            # once: resolve/warm components, generate locks
./cb build --all     # root app + all examples
./cb test            # host unit tests
./cb verify          # the full gate; ./cb report to re-read it
./cb flash --target app   # hardware-verified 2026-11-08; exit 2 + hint only when no port
./cb monitor              # streams the console live; Ctrl-] quits
```

## Raw `idf.py` (IDE-friendly; project owner only while the tree is shared)

Every project is configured by its own files, so plain `idf.py` works with no wrapper flags:

```bash
. ~/.espressif/tools/activate_idf_v6.1.sh
cd examples/01_lcd_hello
idf.py -B /tmp/my-build build        # no -DIDF_TARGET needed (board defaults set esp32s3)
```

Caution: a raw build writes `sdkconfig`/`sdkconfig.old` **into the project directory** (git-ignored
but shared). While multiple agents work in one tree, only the project's owner runs raw builds;
everyone else uses `./cb build` (isolated `-DSDKCONFIG`).

## Project pattern (frozen — copy `examples/01_lcd_hello` for new examples)

`examples/<NN_name>/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../config/sdkconfig.board.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/main/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(<NN_name>)
```

`examples/<NN_name>/main/idf_component.yml` (`path:` is **manifest-relative — THREE ups**):

```yaml
dependencies:
  idf: ">=6.1.0,<6.2.0"
  cb_bsp:
    path: ../../../components/cb_bsp
```

The root app differs only in depth: its CMakeLists lists `config/sdkconfig.board.defaults` +
`main/sdkconfig.app.defaults`, and its manifest uses ONE up (`../components/cb_bsp`).

A project's `CMakeLists.txt` and its `main/sdkconfig*.defaults` file must land **in the same
commit** — IDF raises a FATAL_ERROR when a listed defaults file is missing.

## Partition table — the ONE ruled mechanism

1. The shared `config/sdkconfig.board.defaults` sets `CONFIG_PARTITION_TABLE_CUSTOM=y` but
   **never names a CSV**.
2. Each project has its own tiny defaults file setting only the CSV path, **project-relative**:
   - root app `main/sdkconfig.app.defaults` →
     `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions.csv"`
   - example `examples/<NN>/main/sdkconfig.defaults` →
     `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../config/partitions.csv"`
3. Both defaults files are listed (board first, project second) in `set(SDKCONFIG_DEFAULTS ...)`
   before `include(project.cmake)`; later entries override earlier ones.
4. `-DCONFIG_PARTITION_TABLE_CUSTOM_FILENAME=...` on the command line is verified **inert** —
   never use it. `./cb` may pre-seed `<build>/sdkconfig` as a fallback only, and asserts
   `<build>/partition_table/*.bin` exists with `factory,app,factory,0x10000,3M` and
   `storage,data,fat,0x310000,0x800000`.

`config/partitions.csv` (16 MB flash): `nvs` @0x9000 (24 K) · `phy_init` @0xf000 (4 K) ·
`factory` app @0x10000 (**3 M** — LVGL + radio will never fit the 1 MB default) · `storage` FAT
@0x310000 (**8 M**, future assets/settings). No OTA slots yet — a later product task.

## Board defaults (`config/sdkconfig.board.defaults`) — why each key

```
CONFIG_IDF_TARGET="esp32s3"                # target without -DIDF_TARGET
CONFIG_SPIRAM=y / _MODE_OCT=y / _SPEED_80M=y   # 8 MB OCTAL PSRAM @80 MHz — mandatory (boot loop if wrong)
CONFIG_SPIRAM_USE_MALLOC=y                 # heap allocator may use PSRAM
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y / _FLASHMODE_QIO=y   # 16 MB quad-io flash
CONFIG_PARTITION_TABLE_CUSTOM=y            # CSV named per project (see above)
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y       # logs on the Type-C port (native USB-Serial/JTAG)
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192       # headroom for LVGL/app_main work
CONFIG_FREERTOS_HZ=1000                    # 1 ms tick — item 3's button poller needs finer granularity than the 100 Hz default
```

`./cb verify` step `sdkconfig-assert` mechanically re-checks these keys in every built target.

## Dependencies & offline builds

- Pins are exact and all three components are in the local caches
  (`~/.espressif/tools/components/` mirror + `~/.cache/Espressif/ComponentManager/`), so builds work
  **offline from the first run** on this machine. `./cb deps` repairs a cleared cache (needs
  network once). `./cb verify --offline` proves offline-ness via `unshare -rn` (SKIP with explicit
  wording when namespaces are unavailable).
- `dependencies.lock` **is committed**, per project, by that project's owner (item 1: the owner of
  the root app and `01_lcd_hello`). It is generated by the first successful build; nobody else
  stages lock files.
- **Component wiring (item 2, ruling A3)**: `components/cb_bsp/CMakeLists.txt` lists six explicit
  `SRCS` — `src/cb_bsp.c`, `src/cb_math.c`, `src/cb_button_fsm.c`, `src/cb_lcd.c`,
  `src/cb_backlight.c`, `src/cb_pattern.c` (no GLOB; never list a file that does not exist) — with
  `REQUIRES esp_lcd` and `PRIV_REQUIRES esp_driver_spi esp_driver_ledc`. The managed components
  (lvgl, esp_lvgl_port, led_strip) propagate to consumers **through `idf_component.yml`** (public
  requires); they must never appear as bare `REQUIRES lvgl` / `REQUIRES esp_lvgl_port` — those
  names do not resolve (managed components register as `lvgl__lvgl` /
  `espressif__esp_lvgl_port`). Examples get `lvgl.h` via `PRIV_REQUIRES cb_bsp` alone.
- `./cb verify` step `deps-pins` asserts the exact versions in every committed lock and in the
  manifest; after any manifest/lock change run `./cb deps` once, then the project owner re-commits
  the lock. LVGL adds ~2–4 min cold build time per project.
- Never use `espressif/led_strip` 2.x or LVGL 8 on IDF 6.1 — measured breakage, see
  [troubleshooting.md](troubleshooting.md).

## Adding a new example (items 2+)

1. Copy `examples/01_lcd_hello/` → `examples/<NN_name>/`; adjust `project(<NN_name>)` and the
   partition line depth (examples are always two levels down: `../../config/...`).
2. `main/idf_component.yml` keeps the three-up `cb_bsp` path.
3. Keep `main.c` ≤ ~150 lines; call `cb_bsp_init(...)`; no driver code in examples.
4. `./cb list` discovers it automatically; `./cb verify` builds it as `build:<NN_name>`.
