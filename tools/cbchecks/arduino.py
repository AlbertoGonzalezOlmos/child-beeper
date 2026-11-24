"""Arduino mirror staging (item 7 — AR1/AR8/AR9/AR10/AR15).

The committed Arduino tree never contains copies of the canonical cb_bsp pure
modules. `./cb arduino` (and the verify gate) stages them into the build root:

    ${CB_BUILD_ROOT:-build}/arduino/libraries/CBBoard/
        library.properties, mirror.txt, ...   (committed library files, copied)
        src/
            CBBoard.h        (the committed umbrella, staged into src/)
            <the 12 mirror files, copied byte-for-byte from components/cb_bsp/>

`mirror.txt` (AR8) lists one component-relative path per line; the set is
frozen at exactly REQUIRED_MIRROR — deleting an entry must FAIL the run, never
shrink the mirror silently. This module is the SOLE parser (AR8): staging,
build, install, pin-drift and the tests all consume this API.

pin-drift (AR10) bootstraps the stage only when it is absent (or incomplete)
and otherwise compares the existing staged bytes READ-ONLY, so a mutated
staged byte — or a canonical file changed after the last stage — is a FAIL
("... differs from components/cb_bsp/...") instead of being silently repaired.
`build_arduino` always restages fresh, so compiled sketches are never stale.
"""

from __future__ import annotations

import re
import shutil
from pathlib import Path

from .context import Ctx

MIRROR_FILE = "mirror.txt"
LIBRARY_DIR = "arduino/libraries/CBBoard"
UMBRELLA = "CBBoard.h"

# AR8: the frozen 12-entry mirror set (order = the manifest's frozen order).
REQUIRED_MIRROR: tuple[str, ...] = (
    "include/cb_pins.h",
    "include/cb_math.h",
    "include/cb_button_fsm.h",
    "include/cb_qmi8658_regs.h",
    "include/cb_rgb_math.h",
    "include/cb_battery_math.h",
    "include/cb_imu_math.h",
    "src/cb_math.c",
    "src/cb_button_fsm.c",
    "src/cb_rgb_math.c",
    "src/cb_battery_math.c",
    "src/cb_imu_math.c",
)

_MIRROR_BASENAMES = frozenset(Path(rel).name for rel in REQUIRED_MIRROR)
_ENTRY_RE = re.compile(r"^(?:include/[a-z0-9_]+\.h|src/[a-z0-9_]+\.c)$")


def library_root(ctx: Ctx) -> Path:
    """The committed library directory (owner: mimo-v26-pro)."""
    return ctx.path(LIBRARY_DIR)


def manifest_path(ctx: Ctx) -> Path:
    return library_root(ctx) / MIRROR_FILE


def staged_libraries_dir(ctx: Ctx) -> Path:
    """The staged libraries root arduino-cli is pointed at (AR9)."""
    return ctx.build_root / "arduino" / "libraries"


def staged_library(ctx: Ctx) -> Path:
    return staged_libraries_dir(ctx) / "CBBoard"


def canonical(ctx: Ctx, rel: str) -> Path:
    return ctx.path("components/cb_bsp") / rel


def parse_manifest(path: Path) -> tuple[tuple[str, ...] | None, str | None]:
    """Parse mirror.txt; returns (entries, error). Fail-closed (AR8)."""
    if not path.is_file():
        return None, f"{MIRROR_FILE} missing ({LIBRARY_DIR}/{MIRROR_FILE})"
    entries: list[str] = []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not _ENTRY_RE.match(line):
            return None, (
                f"{MIRROR_FILE} line {lineno}: '{line}' "
                "(expected include/*.h or src/*.c)"
            )
        if line in entries:
            return None, f"{MIRROR_FILE} line {lineno}: duplicate entry '{line}'"
        entries.append(line)
    listed = frozenset(entries)
    required = frozenset(REQUIRED_MIRROR)
    for rel in REQUIRED_MIRROR:
        if rel not in listed:
            return None, f"{MIRROR_FILE} set mismatch: missing required entry {rel}"
    for rel in entries:
        if rel not in required:
            return None, f"{MIRROR_FILE} set mismatch: unexpected entry {rel}"
    return tuple(entries), None


def _committed_mirror_copies(ctx: Ctx) -> list[Path]:
    """AR1/AR2 stale rule: no mirror-named file may be committed under arduino/."""
    tree = ctx.path("arduino")
    if not tree.is_dir():
        return []
    return sorted(
        p
        for p in tree.rglob("*")
        if p.is_file() and p.name in _MIRROR_BASENAMES
    )


def stage_library(ctx: Ctx) -> tuple[Path | None, str | None]:
    """Wipe-and-copy the staged CBBoard library (fresh; AR9). Returns (dir, error)."""
    lib = library_root(ctx)
    if not lib.is_dir():
        return None, f"{LIBRARY_DIR} missing"
    entries, err = parse_manifest(manifest_path(ctx))
    if err is not None:
        return None, err
    assert entries is not None
    for rel in entries:
        if not canonical(ctx, rel).is_file():
            return None, f"manifest names missing file: components/cb_bsp/{rel}"
    # AR15: the library must never contain an .ino (sketch discovery would
    # treat it as a buildable sketch).
    for ino in sorted(lib.rglob("*.ino")):
        rel = ino.relative_to(ctx.root).as_posix()
        return None, f"{rel}: library must never contain an .ino (AR15)"

    staged = staged_library(ctx)
    if staged.exists():
        shutil.rmtree(staged)
    staged_src = staged / "src"
    staged_src.mkdir(parents=True, exist_ok=True)
    for item in sorted(lib.iterdir()):
        if item.is_dir():
            # Committed subdirectories (e.g. a src/ with the umbrella) are copied
            # as-is; .ino was already rejected above.
            shutil.copytree(item, staged / item.name, dirs_exist_ok=True)
            continue
        if item.name == UMBRELLA:
            # The recursive library layout needs the umbrella inside src/ so
            # `#include <CBBoard.h>` and the mirrored includes resolve together.
            shutil.copy2(item, staged_src / item.name)
            continue
        shutil.copy2(item, staged / item.name)
    for rel in entries:
        shutil.copy2(canonical(ctx, rel), staged_src / Path(rel).name)
    return staged, None


def stage_is_current(ctx: Ctx) -> bool:
    """True when the staged library exists with every mirror file present."""
    staged_src = staged_library(ctx) / "src"
    if not staged_src.is_dir():
        return False
    return all((staged_src / Path(rel).name).is_file() for rel in REQUIRED_MIRROR)


def pin_drift(ctx: Ctx) -> tuple[str, str]:
    """The real pin-drift check (AR10): manifest set + committed-copy ban +
    staged-vs-canonical byte equality. SKIP only while arduino/ is absent."""
    tree = ctx.path("arduino")
    if not tree.is_dir():
        return "SKIP", "no arduino/ tree yet (item 7)"
    lib = library_root(ctx)
    if not lib.is_dir():
        return "FAIL", f"{LIBRARY_DIR} missing"
    entries, err = parse_manifest(manifest_path(ctx))
    if err is not None:
        return "FAIL", err
    assert entries is not None
    stale = _committed_mirror_copies(ctx)
    if stale:
        rel = stale[0].relative_to(ctx.root).as_posix()
        return "FAIL", f"committed mirror copy {rel} — stage at build, nothing committed (AR1)"
    # Bootstrap only when the stage is absent/incomplete; otherwise compare the
    # existing bytes read-only so staleness is reported, not repaired (AR10).
    if not stage_is_current(ctx):
        _, err = stage_library(ctx)
        if err is not None:
            return "FAIL", err
    staged_src = staged_library(ctx) / "src"
    for rel in entries:
        got = (staged_src / Path(rel).name).read_bytes()
        want = canonical(ctx, rel).read_bytes()
        if got != want:
            staged_rel = (staged_src / Path(rel).name).relative_to(ctx.build_root).as_posix()
            return "FAIL", f"{staged_rel} differs from components/cb_bsp/{rel}"
    return "PASS", f"{len(entries)} mirror file(s) staged byte-equal"
