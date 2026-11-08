"""Building IDF/Arduino targets, sdkconfig + partition-table assertions.

Rulings honoured:
  R3   single partition mechanism (per-project defaults file); `./cb` may pre-seed sdkconfig as a
       *fallback only*; asserts the partition binary and the two content lines.
  R9   -DSDKCONFIG=<build_root>/<target>/sdkconfig so agents never share a project-dir sdkconfig.
"""

from __future__ import annotations

import re
import shlex
import struct
from dataclasses import dataclass, field
from pathlib import Path

from .context import PARTITIONS_CSV, Ctx
from .proc import Runner
from .sizes import artifacts, primary_bin
from .targets import ARD_FQBN, Target, build_dir

# ITEM2 ruling A9: these two targets are the display stack; LVGL config must be present and correct
# there (absence is a FAIL, not a skip). Other targets keep the presence-conditional behaviour.
LVGL_REQUIRED_TARGETS = ("app", "01_lcd_hello")

SDKCONFIG_LVGL_KEYS = (
    ("CONFIG_LV_CONF_SKIP=y", "LVGL configured by Kconfig (no stray lv_conf.h)"),
    ("CONFIG_LV_COLOR_DEPTH_16=y", "RGB565, matches the 16-bit SPI panel"),
    ("CONFIG_LV_COLOR_DEPTH=16", "derived colour-depth value"),
    ("CONFIG_LV_OS_NONE=y", "esp_lvgl_port owns the LVGL task (LV_OS_FREERTOS must not be set)"),
    ("CONFIG_LV_FONT_DEFAULT_MONTSERRAT_14=y", "default font selected"),
    ("CONFIG_LV_FONT_MONTSERRAT_14=y", "default font compiled in"),
    ("CONFIG_LV_USE_LABEL=y", "01_lcd_hello draws a text label"),
    ("CONFIG_LV_USE_IMAGE=y", "01_lcd_hello draws the diagnostic pattern as an lv_image"),
)
SDKCONFIG_LVGL_FORBIDDEN = (
    ("CONFIG_LV_CONF_MINIMAL=y", "minimal config drops the fonts/widgets the example needs"),
)

SDKCONFIG_REQUIRED = (
    "CONFIG_SPIRAM=y",
    "CONFIG_SPIRAM_MODE_OCT=y",
    "CONFIG_SPIRAM_SPEED_80M=y",
    "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y",
    "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y",
    "CONFIG_PARTITION_TABLE_CUSTOM=y",
)

# --- partition table decoding (R3: assert the built table, not just the config) ---------------
PART_BIN_RELPATH = "partition_table/partition-table.bin"
PART_MAGIC = 0x50AA  # ESP-IDF partition entry magic (0xAA50, little-endian on flash)
PART_ENTRY = struct.Struct("<HBBII16sI")  # magic, type, subtype, offset, size, label[16], flags
PART_TYPES = {0: "app", 1: "data"}
PART_SUBTYPES = {
    (0, 0): "factory",
    (1, 0): "ota",
    (1, 1): "phy",
    (1, 2): "nvs",
    (1, 4): "nvs_keys",
    (1, 0x81): "fat",
    (1, 0x82): "spiffs",
    (1, 0x83): "littlefs",
}
EXPECTED_FACTORY = {"name": "factory", "type": "app", "subtype": "factory", "offset": 0x10000}
EXPECTED_STORAGE = {"name": "storage", "type": "data", "subtype": "fat", "offset": 0x310000}


def _size_matches(value: str, expected: int) -> bool:
    """Accept both raw byte counts (binary decode) and gen_esp32part's '3M'/'8M' forms."""
    text = str(value).strip().rstrip(",")
    if text.lower().startswith("0x"):
        try:
            return int(text, 16) == expected
        except ValueError:
            return False
    match = re.fullmatch(r"(\d+)\s*([KMG]?)", text, re.IGNORECASE)
    if not match:
        return False
    number = int(match.group(1))
    unit = match.group(2).upper()
    multiplier = {"": 1, "K": 1024, "M": 1024 * 1024, "G": 1024 ** 3}[unit]
    if multiplier != 1:
        return number * multiplier == expected
    # unit-less: bytes exactly, or KiB/MiB shorthand used by older tools
    return number == expected or number * 1024 == expected or number * 1024 * 1024 == expected


def _row_ok(row: dict, expected: dict, size: int) -> bool:
    if row.get("name") != expected["name"]:
        return False
    if row.get("type") != expected["type"] or row.get("subtype") != expected["subtype"]:
        return False
    try:
        if int(str(row.get("offset")), 0) != expected["offset"]:
            return False
    except (TypeError, ValueError):
        return False
    return _size_matches(str(row.get("size", "")), size)


def parse_partition_binary(path: Path) -> list[dict]:
    """Decode partition-table.bin (32-byte little-endian entries, magic 0xAA50)."""
    rows: list[dict] = []
    data = path.read_bytes()
    for base in range(0, len(data) - PART_ENTRY.size + 1, PART_ENTRY.size):
        magic, ptype, subtype, offset, size, label, _flags = PART_ENTRY.unpack_from(data, base)
        if magic != PART_MAGIC:
            break
        rows.append(
            {
                "name": label.split(b"\0", 1)[0].decode("ascii", "replace"),
                "type": PART_TYPES.get(ptype, str(ptype)),
                "subtype": PART_SUBTYPES.get((ptype, subtype), hex(subtype)),
                "offset": hex(offset),
                "size": str(size),
            }
        )
    return rows


def parse_gen_esp32part(text: str) -> list[dict]:
    """Parse the CSV that gen_esp32part.py prints for a binary input."""
    rows: list[dict] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = [field.strip() for field in stripped.split(",")]
        if len(fields) < 5:
            continue
        rows.append(
            {
                "name": fields[0],
                "type": fields[1],
                "subtype": fields[2],
                "offset": fields[3],
                "size": fields[4],
            }
        )
    return rows


def read_partition_rows(ctx: Ctx, runner: Runner, bdir: Path) -> tuple[list[dict], str]:
    """Authoritative rows: gen_esp32part.py first, binary decode second, build log last."""
    bin_path = bdir / PART_BIN_RELPATH
    if not bin_path.is_file():
        return [], f"missing {bin_path}"
    snippet = (
        'python "$IDF_PATH/components/partition_table/gen_esp32part.py" '
        + shlex.quote(str(bin_path))
    )
    try:
        result = runner.run_idf_snippet(snippet, cwd=ctx.root, timeout=180.0)
        if result.ok:
            rows = parse_gen_esp32part(result.out)
            if rows:
                return rows, "gen_esp32part.py"
    except Exception:  # noqa: BLE001 - fall back to the binary decoder
        pass
    try:
        rows = parse_partition_binary(bin_path)
        if rows:
            return rows, "binary decode"
    except OSError as exc:
        return [], f"cannot read {bin_path}: {exc}"
    return [], "no decodable partition rows"


def _rows_from_build_log(ctx: Ctx, target: Target) -> list[dict]:
    log = ctx.log_path(f"build:{target.name}")
    if not log.is_file():
        return []
    rows: list[dict] = []
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = [field.strip() for field in stripped.split(",")]
        if len(fields) >= 5 and fields[0] in ("factory", "storage"):
            rows.append(
                {"name": fields[0], "type": fields[1], "subtype": fields[2],
                 "offset": fields[3], "size": fields[4]}
            )
    return rows


ERROR_HINTS = (
    "CMake Error",
    "FATAL_ERROR",
    "ninja: error",
    "error:",
    "No such option",
    "Could not find",
    "not found",
)


def extract_error(text: str, limit: int = 3) -> str:
    """Return the first few error-looking lines so a failed step is self-explanatory."""
    picked: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if any(hint in stripped for hint in ERROR_HINTS):
            picked.append(stripped)
            if len(picked) >= limit:
                break
    return " | ".join(picked)


FACTORY_LINE = re.compile(r"^factory,\s*app,\s*factory,\s*0x10000,\s*3M", re.MULTILINE)
STORAGE_LINE = re.compile(
    r"^storage,\s*data,\s*fat,\s*0x310000,\s*(?:0x800000|8M)", re.MULTILINE
)


@dataclass
class BuildOutcome:
    ok: bool
    target: Target
    build_dir: Path
    log: Path
    detail: str
    extra: dict = field(default_factory=dict)


def project_dir(target: Target) -> Path:
    return target.path


def partition_defaults_file(target: Target) -> Path:
    """Per-project defaults file that carries the project-relative CSV path (R3)."""
    if target.kind == "app":
        return target.path / "main" / "sdkconfig.app.defaults"
    return target.path / "main" / "sdkconfig.defaults"


def _preseed_sdkconfig(ctx: Ctx, target: Target, build_dir: Path) -> None:
    """Fallback only: if a project has no partition defaults file, seed the build sdkconfig."""
    sdkconfig = build_dir / "sdkconfig"
    if sdkconfig.exists():
        return
    defaults = partition_defaults_file(target)
    if defaults.is_file():
        return  # the project owns the mechanism; do not shadow it
    csv = ctx.path(PARTITIONS_CSV)
    if not csv.is_file():
        return
    sdkconfig.write_text(
        "# generated by ./cb (fallback: project has no partition defaults file)\n"
        "CONFIG_PARTITION_TABLE_CUSTOM=y\n"
        f'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{csv}"\n',
        encoding="utf-8",
    )


def _write_log(ctx: Ctx, step_id: str, text: str) -> Path:
    log = ctx.log_path(step_id)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text(text, encoding="utf-8")
    return log


def build_idf(ctx: Ctx, runner: Runner, target: Target, *, timeout: float = 3600.0) -> BuildOutcome:
    bdir = build_dir(ctx, target)
    bdir.mkdir(parents=True, exist_ok=True)
    _preseed_sdkconfig(ctx, target, bdir)
    sdkconfig = bdir / "sdkconfig"
    args = [
        "-C", str(project_dir(target)),
        "-B", str(bdir),
        f"-DSDKCONFIG={sdkconfig}",
        "-DIDF_TARGET=esp32s3",
        "build",
    ]
    result = runner.run_idf(args, cwd=ctx.root, timeout=timeout)
    log = _write_log(ctx, f"build:{target.name}", (result.out or "") + (result.err or ""))
    binary = primary_bin(bdir)
    ok = result.ok and binary is not None
    if ok:
        detail = f"{binary.name} {binary.stat().st_size} bytes; log {log}"
    else:
        reason = extract_error((result.out or "") + (result.err or "")) or result.tail(4) or "(no output)"
        detail = f"build failed: {reason} (log {log})"
    extra: dict = {"build_dir": str(bdir), "log": str(log)}
    if binary is not None:
        extra["artifacts"] = artifacts(bdir)
    return BuildOutcome(ok, target, bdir, log, detail, extra)


def build_arduino(ctx: Ctx, runner: Runner, target: Target) -> BuildOutcome:
    bdir = build_dir(ctx, target)
    bdir.mkdir(parents=True, exist_ok=True)
    argv = [
        "compile",
        "-b", ARD_FQBN,
        "--build-path", str(bdir),
        "--warnings", "all",
        "--no-color",
    ]
    libraries = ctx.path("arduino") / "libraries"
    if libraries.is_dir():
        argv += ["--libraries", str(libraries)]
    argv.append(str(target.path))
    result = runner.run_arduino(argv, cwd=ctx.root)
    log = _write_log(ctx, f"arduino:{target.name}", (result.out or "") + (result.err or ""))
    ok = result.ok
    if ok:
        detail = f"compiled; log {log}"
    else:
        reason = extract_error((result.out or "") + (result.err or "")) or result.tail(4)
        detail = f"compile failed: {reason} (log {log})"
    return BuildOutcome(ok, target, bdir, log, detail, {"build_dir": str(bdir), "log": str(log)})


def build_target(ctx: Ctx, runner: Runner, target: Target) -> BuildOutcome:
    if target.kind == "arduino":
        return build_arduino(ctx, runner, target)
    return build_idf(ctx, runner, target)


def lvgl_present(sdkconfig_text: str) -> bool:
    return "CONFIG_LV_" in sdkconfig_text


def assert_sdkconfig(ctx: Ctx, runner: Runner, target: Target, bdir: Path) -> tuple[bool, str, dict]:
    """R3: config keys + partition binary + the two partition content lines."""
    sdkconfig = bdir / "sdkconfig"
    if not sdkconfig.is_file():
        return False, f"{target.name}: no sdkconfig at {sdkconfig}", {}
    text = sdkconfig.read_text(encoding="utf-8", errors="replace")
    missing = [key for key in SDKCONFIG_REQUIRED if key not in text]
    lvgl_checked = lvgl_present(text) or target.name in LVGL_REQUIRED_TARGETS
    missing_lvgl: list[str] = []
    forbidden_lvgl: list[str] = []
    if lvgl_checked:
        if not lvgl_present(text):
            # A9: for app/01_lcd_hello the absence of LVGL config is a failure, not a skip.
            missing_lvgl = ["<no CONFIG_LV_* in sdkconfig: LVGL is not in this build>"]
        else:
            missing_lvgl = [key for key, _why in SDKCONFIG_LVGL_KEYS if key not in text]
        forbidden_lvgl = [key for key, _why in SDKCONFIG_LVGL_FORBIDDEN if key in text]
    partition_bin = bdir / PART_BIN_RELPATH
    problems: list[str] = []
    if missing:
        problems.append("missing keys: " + ", ".join(missing))
    if missing_lvgl:
        problems.append("missing LVGL keys: " + ", ".join(missing_lvgl))
    if forbidden_lvgl:
        problems.append("forbidden LVGL keys: " + ", ".join(forbidden_lvgl))
    rows, source = read_partition_rows(ctx, runner, bdir)
    if not partition_bin.is_file():
        problems.append(f"missing {partition_bin}")
    ok_table = (
        any(_row_ok(row, EXPECTED_FACTORY, 3 * 1024 * 1024) for row in rows)
        and any(_row_ok(row, EXPECTED_STORAGE, 8 * 1024 * 1024) for row in rows)
    )
    if not ok_table:
        log_rows = _rows_from_build_log(ctx, target)  # OR-fallback only
        if log_rows:
            ok_table = (
                any(_row_ok(row, EXPECTED_FACTORY, 3 * 1024 * 1024) for row in log_rows)
                and any(_row_ok(row, EXPECTED_STORAGE, 8 * 1024 * 1024) for row in log_rows)
            )
            source = "build log (fallback)"
    if not ok_table:
        problems.append(
            "partition table does not match factory@0x10000 3M + storage@0x310000 8M "
            f"(source: {source})"
        )
    extra = {
        "target": target.name,
        "partition_bin": str(partition_bin) if partition_bin.is_file() else None,
        "partition_rows": rows,
        "partition_source": source,
        "missing_keys": missing,
        "lvgl_checked": lvgl_checked,
        "missing_lvgl_keys": missing_lvgl,
        "forbidden_lvgl_keys": forbidden_lvgl,
    }
    if problems:
        return False, f"{target.name}: " + "; ".join(problems), extra
    lvgl_note = " + LVGL" if lvgl_checked else ""
    return True, f"{target.name}: keys{lvgl_note} + partition table verified ({source})", extra
