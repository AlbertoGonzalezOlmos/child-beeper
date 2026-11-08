# AGENTS.md — working rules for coding agents (and humans)

## Canonical commands

- ESP-IDF env: `. ~/.espressif/tools/activate_idf_v6.1.sh` (IDF v6.1; never hardcode absolute
  home paths in tracked files).
- `./cb doctor` — toolchain check · `./cb verify` — the build gate (host tests + builds +
  audits; runs with or without a board attached — flashing is the only part that needs one).
- `./cb build app` — root app · `./cb build <example-dir-name>` (e.g. `01_lcd_hello`) ·
  `./cb test` — host unit tests (`make -C tests/host test`).
- Raw build (IDE-friendly), inside any project dir: `idf.py -B <dir> build` — the target comes
  from `config/sdkconfig.board.defaults`; no `-DIDF_TARGET` needed.
- Build outputs go to `${CB_BUILD_ROOT:-build}/<target>`. When several agents share the tree,
  each exports `CB_BUILD_ROOT=build/<agent-name>` and never touches another agent's build dirs.
- Flash/monitor: `./cb flash` / `./cb monitor` — they check ports **before** building and exit 2
  (with the BOOT+RESET recovery hint) when no board is attached.

## Layout

- `main/` — root "hello beeper" app (thin: init the BSP + heartbeat over USB serial).
- `components/cb_bsp/` — the board support package; one public header per peripheral (`cb_*.h`).
- `examples/<NN_name>/` — standalone IDF projects, one per peripheral. Copy
  `examples/01_lcd_hello` as the pattern (see its README).
- `config/` — shared `sdkconfig.board.defaults` + `partitions.csv` (3 MB factory + 8 MB FAT).
- `tests/host/` — host-compiled unit tests (pure-C BSP helpers).
- `arduino/` (later), `tools/` (`./cb` CLI), `docs/`.

## Project pattern rules (frozen by the item-1 contract)

- Every project's `CMakeLists.txt` sets `SDKCONFIG_DEFAULTS` to **two** entries, in order, before
  `include(project.cmake)`: `config/sdkconfig.board.defaults` (shared) then the project's own
  `main/sdkconfig*.defaults` (which sets only `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME`,
  project-relative). The shared file never names a CSV; there is no other partition mechanism.
- Component-manager `path:` is **manifest-relative**: root app `main/idf_component.yml` uses
  `../components/cb_bsp` (one up); examples use `../../../components/cb_bsp` (three ups).
- A raw `idf.py` build writes `sdkconfig` into the *project dir* — only that project's owner may
  run raw builds while several agents share the tree (`./cb` isolates via `-DSDKCONFIG`).
- `dependencies.lock` is committed **by the project's owner** after that project's first
  successful build. It is never gitignored.

## Rules

- One writer per file — see the interface contract for ownership. `git add` only your own paths;
  never `git add -A`, never `git commit -a`. Commit format: `toolkit(itemN) <slice>: <one line>`.
- Public names/signatures in `components/cb_bsp/include/` are frozen by the contract — change them
  only through a review-round decision, never unilaterally.
- **Never claim something is hardware-verified without the board.** Say build-verified.
  Hardware-pending facts go to `docs/verification.md`.
- Forbidden constructs (legacy IDF-6 APIs, LVGL 8 symbols, `ledcSetup`, `neopixelWrite`,
  SD auto-format, `ESP_ERROR_CHECK` in `cb_bsp` on optional hardware) are listed in
  `tools/cbchecks/` and enforced by `./cb verify` — do not add them to tracked sources.
- Untouchable: `LICENSE`, `eim_config.toml`, `.vscode/`, `/tmp/herdr-crew/reference/**`.
