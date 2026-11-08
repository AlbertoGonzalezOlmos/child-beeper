"""Repository/build context resolution.

Never hardcodes a home directory: the repo root is derived from this file's location and the IDF
activation script is discovered from the environment, $IDF_PATH or ~/.espressif.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path

from .errors import EnvError

# Paths (relative to the repo root) that item 1 expects to exist.
ROOT_APP_CMAKE = "CMakeLists.txt"
ROOT_APP_MAIN = "main"
BSP_DIR = "components/cb_bsp"
BSP_PINS = "components/cb_bsp/include/cb_pins.h"
BOARD_DEFAULTS = "config/sdkconfig.board.defaults"
PARTITIONS_CSV = "config/partitions.csv"
HOST_TESTS_MAKEFILE = "tests/host/Makefile"
EXAMPLES_DIR = "examples"
ARDUINO_DIR = "arduino"


def repo_root() -> Path:
    """<repo>/tools/cbchecks/context.py -> <repo>"""
    return Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class Ctx:
    root: Path
    build_root: Path
    offline: bool = False
    no_arduino: bool = False

    def path(self, rel: str) -> Path:
        return self.root / rel

    def target_build_dir(self, name: str) -> Path:
        return self.build_root / name

    def log_path(self, step_id: str) -> Path:
        safe = step_id.replace(":", "_").replace("/", "_")
        return self.build_root / "logs" / (safe + ".log")

    def abs_build_root(self) -> str:
        return str(self.build_root)


def _resolve_build_root(root: Path) -> Path:
    raw = os.environ.get("CB_BUILD_ROOT", "").strip()
    if not raw:
        return root / "build"
    candidate = Path(raw)
    if not candidate.is_absolute():
        candidate = root / candidate
    return candidate


def load_ctx(*, offline: bool = False, no_arduino: bool = False) -> Ctx:
    root = repo_root()
    return Ctx(root=root, build_root=_resolve_build_root(root), offline=offline, no_arduino=no_arduino)


def find_idf_activate() -> Path | None:
    """Locate the ESP-IDF activation script without hardcoding a home directory."""
    override = os.environ.get("CB_IDF_ACTIVATE", "").strip()
    if override:
        p = Path(override).expanduser()
        return p if p.is_file() else None

    candidates: list[Path] = []
    idf_path = os.environ.get("IDF_PATH", "").strip()
    if idf_path:
        base = Path(idf_path).expanduser().parent / "tools"
        candidates.append(base / "activate_idf_v6.1.sh")
        candidates.extend(sorted(base.glob("activate_idf_v*.sh")))

    espressif = Path.home() / ".espressif" / "tools"
    candidates.append(espressif / "activate_idf_v6.1.sh")
    candidates.extend(sorted(espressif.glob("activate_idf_v*.sh")))

    for cand in candidates:
        if cand.is_file():
            return cand
    return None


def require_idf_activate() -> Path:
    script = find_idf_activate()
    if script is None:
        raise EnvError(
            "ESP-IDF activation script not found (looked at $CB_IDF_ACTIVATE, $IDF_PATH/../tools, "
            "~/.espressif/tools). Run './cb doctor' for details."
        )
    return script


def arduino_cli() -> Path | None:
    override = os.environ.get("CB_ARDUINO_CLI", "").strip()
    if override:
        p = Path(override).expanduser()
        return p if p.is_file() else None
    local = Path.home() / ".local" / "bin" / "arduino-cli"
    if local.is_file():
        return local
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        if not directory:
            continue
        cand = Path(directory) / "arduino-cli"
        if cand.is_file():
            return cand
    return None
