"""Static audits: forbidden constructs, absolute home paths, Arduino pin-header drift.

Rulings honoured (CONTRACT R7):
  * construct rules scan only code/CMake files under main/, components/, examples/, tests/, tools/;
    docs/** and *.md are exempt (they must be able to quote forbidden symbols);
  * the absolute-path rule scans all tracked text files, with one allowlist entry: eim_config.toml.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from .context import BSP_PINS, Ctx

PASS = "PASS"
FAIL = "FAIL"
SKIP = "SKIP"

CONSTRUCT_DIRS = ("main", "components", "examples", "tests", "tools")
CODE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".ino", ".cc"}
CMAKE_NAMES = {"CMakeLists.txt"}
CMAKE_SUFFIXES = {".cmake"}

TEXT_SUFFIXES = CODE_SUFFIXES | {
    ".py", ".sh", ".md", ".txt", ".yml", ".yaml", ".toml", ".csv", ".json", ".cfg", ".ini",
} | CMAKE_SUFFIXES

SKIP_DIRS = {".git", "build", ".cache", "__pycache__", "managed_components", "node_modules", ".vscode"}
ABS_PATH_ALLOW = {"eim_config.toml"}
ALLOW_FILE_MARKER = "cbcheck:allow-file"
ALLOW_LINE_MARKER = "cbcheck:allow"

# Patterns are assembled from fragments so this module does not match its own rules.
_I2C_LEGACY = "driver/i2c" + ".h"
_LCD_ENDIAN_T = "lcd_color_rgb" + "_endian_t"
_LCD_ENDIAN_M = "LCD_RGB" + "_ENDIAN_"
_LV_DISP_DRV = "lv_disp" + "_drv_t"
_LV_DRAW_BUF = "lv_disp" + "_draw_buf_t"
_LV_SCR_ACT = "lv_scr" + "_act\\s*\\("
_LEDC_SETUP = "ledc" + "Setup\\s*\\("
_LEDC_ATTACH_PIN = "ledc" + "AttachPin\\s*\\("
_NEOPIXEL = "neopixel" + "Write\\s*\\("
_SD_AUTOFMT = "format_if_mount" + "_failed\\s*=\\s*true"
_ESP_ERROR_CHECK = "ESP_ERROR" + "_CHECK\\s*\\("

RULES: tuple[tuple[str, str], ...] = (
    ("driver/i2c.h (legacy I2C API removed in IDF 6)", _I2C_LEGACY),
    ("lcd_color_rgb_endian_t (renamed in IDF 6)", _LCD_ENDIAN_T),
    ("LCD_RGB_ENDIAN_ (renamed in IDF 6)", _LCD_ENDIAN_M),
    ("lv_disp_drv_t (LVGL 8 API)", _LV_DISP_DRV),
    ("lv_disp_draw_buf_t (LVGL 8 API)", _LV_DRAW_BUF),
    ("lv_scr_act( (LVGL 8 API)", _LV_SCR_ACT),
    ("ledcSetup( (Arduino core 2.x API)", _LEDC_SETUP),
    ("ledcAttachPin( (Arduino core 2.x API)", _LEDC_ATTACH_PIN),
    ("neopixelWrite( (deprecated in core 3.x)", _NEOPIXEL),
    ("format_if_mount_failed = true (data-loss hazard)", _SD_AUTOFMT),
)

ESP_ERROR_CHECK_RULE = "ESP_ERROR_CHECK( in cb_bsp (use error returns, or '// cbcheck:allow')"

# Hardware-proven invariants for the display bring-up (item-2). Both were found by flashing:
#   * esp_lvgl_port asserts `disp_cfg->io_handle != NULL` (esp_lvgl_port_disp.c:120) -> panic loop,
#   * `buff_dma && buff_spiram` is rejected with ESP_ERR_NOT_SUPPORTED on this SoC.
# They are single-file, source-level facts the sdkconfig assertions cannot see.
LCD_INVARIANTS_FILE = "components/cb_bsp/src/cb_lcd.c"
LCD_IO_HANDLE_RULE = "cb_lcd.c display config must set .io_handle to a non-NULL handle (esp_lvgl_port asserts)"
LCD_BUFF_SPIRAM_RULE = "cb_lcd.c must not set .buff_spiram = true (rejected on this SoC: no PSRAM DMA)"
LCD_IO_HANDLE_ASSIGNED = re.compile(r"\.io_handle\s*=\s*(?!NULL\b|0\b|nullptr\b)[A-Za-z_(]")
LCD_BUFF_SPIRAM_TRUE = re.compile(r"\.buff_spiram\s*=\s*(?:true|1)\b")
BSP_ESP_CHECK_SCOPE = "components/cb_bsp"

# A real absolute home path needs a user segment; prose that merely warns against hardcoding
# absolute user-home paths must not be flagged.
ABS_PATH_RE = re.compile(r"/(?:home|Users)/[A-Za-z0-9._-]+/")
ALLOW_FILE = "tools/cbchecks/staticchecks.py"


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    text: str

    def render(self) -> str:
        return f"{self.path}:{self.line}: {self.rule}: {self.text.strip()[:120]}"


def _walk_files(root: Path, dirs: Sequence[str], suffixes: set[str], names: set[str]) -> Iterable[Path]:
    for rel in dirs:
        base = root / rel
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file():
                continue
            rel_parts = path.relative_to(root).parts
            if any(part in SKIP_DIRS for part in rel_parts):
                continue
            if path.suffix in suffixes or path.name in names:
                yield path


def _allow_file(path: Path, root: Path) -> bool:
    rel = path.relative_to(root).as_posix()
    if rel in ABS_PATH_ALLOW or rel == ALLOW_FILE:
        return True
    try:
        head = path.read_text(encoding="utf-8", errors="replace").splitlines()[:8]
    except OSError:
        return False
    return any(ALLOW_FILE_MARKER in line for line in head)


def scan_forbidden(ctx: Ctx) -> list[Finding]:
    findings: list[Finding] = []
    for path in _walk_files(ctx.root, CONSTRUCT_DIRS, CODE_SUFFIXES, CMAKE_NAMES):
        if path.suffix not in CODE_SUFFIXES and path.name not in CMAKE_NAMES:
            continue
        if _allow_file(path, ctx.root):
            continue
        rel = path.relative_to(ctx.root).as_posix()
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, start=1):
            if ALLOW_LINE_MARKER in line:
                continue
            for rule, pattern in RULES:
                if re.search(pattern, line):
                    findings.append(Finding(rel, number, rule, line))
            if rel.startswith(BSP_ESP_CHECK_SCOPE) and rel.endswith(".c"):
                if re.search(_ESP_ERROR_CHECK, line):
                    findings.append(Finding(rel, number, ESP_ERROR_CHECK_RULE, line))
    # .cmake files anywhere under the scanned dirs
    for path in _walk_files(ctx.root, CONSTRUCT_DIRS, CMAKE_SUFFIXES, set()):
        if _allow_file(path, ctx.root):
            continue
        rel = path.relative_to(ctx.root).as_posix()
        for number, line in enumerate(
            path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1
        ):
            if ALLOW_LINE_MARKER in line:
                continue
            for rule, pattern in RULES:
                if re.search(pattern, line):
                    findings.append(Finding(rel, number, rule, line))
    return findings


def scan_lcd_invariants(ctx: Ctx) -> list[Finding]:
    """Item-2 hardware-proven invariants inside cb_lcd.c (see LCD_* constants)."""
    path = ctx.path(LCD_INVARIANTS_FILE)
    if not path.is_file():
        return []  # other items may restructure; absence is handled by the build steps
    rel = LCD_INVARIANTS_FILE
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []
    findings: list[Finding] = []
    io_assigned = False
    for number, line in enumerate(lines, start=1):
        if ALLOW_LINE_MARKER in line:
            continue
        if LCD_IO_HANDLE_ASSIGNED.search(line):
            io_assigned = True
        if LCD_BUFF_SPIRAM_TRUE.search(line):
            findings.append(Finding(rel, number, LCD_BUFF_SPIRAM_RULE, line))
    if not io_assigned:
        findings.append(Finding(rel, 0, LCD_IO_HANDLE_RULE, "<no '.io_handle = <handle>' assignment found>"))
    return findings


def scan_abs_paths(ctx: Ctx) -> list[Finding]:
    findings: list[Finding] = []
    for path in _walk_files(ctx.root, (".",), TEXT_SUFFIXES, CMAKE_NAMES):
        rel = path.relative_to(ctx.root).as_posix()
        if rel.split("/")[0] in SKIP_DIRS:
            continue
        if _allow_file(path, ctx.root):
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for number, line in enumerate(lines, start=1):
            if ABS_PATH_RE.search(line):
                findings.append(Finding(rel, number, "absolute home path in a tracked source", line))
    return findings


def pin_drift(ctx: Ctx) -> tuple[str, str]:
    """Compare Arduino copies of cb_pins.h with the canonical header (R7 / R6 SKIP rule)."""
    canonical = ctx.path(BSP_PINS)
    arduino = ctx.path("arduino")
    if not arduino.is_dir():
        return SKIP, "no arduino/ tree yet (item 7)"
    if not canonical.is_file():
        return FAIL, f"{BSP_PINS} missing (owner: kimi-k31-pro)"
    copies = sorted(p for p in arduino.rglob("cb_pins.h") if p.is_file())
    if not copies:
        return SKIP, "arduino/ present but no cb_pins.h copy yet"
    reference = canonical.read_bytes()
    for copy in copies:
        rel = copy.relative_to(ctx.root).as_posix()
        if copy.read_bytes() != reference:
            return FAIL, f"{rel} differs from {BSP_PINS}"
    return PASS, f"{len(copies)} copy(ies) byte-equal"


def audit_static(ctx: Ctx) -> tuple[str, str]:
    findings = scan_forbidden(ctx) + scan_abs_paths(ctx) + scan_lcd_invariants(ctx)
    if findings:
        first = "; ".join(f.render() for f in findings[:5])
        more = f" (+{len(findings) - 5} more)" if len(findings) > 5 else ""
        return FAIL, f"{len(findings)} finding(s): {first}{more}"
    return PASS, "no forbidden constructs, no absolute home paths"
