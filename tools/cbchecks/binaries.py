"""Build-artifact helpers used by `./cb flash` and `./cb monitor`.

Two jobs:
  * find the application ELF that `idf.py monitor` will actually use (`project_description.json`'s
    `app_elf` inside the build dir — that is also what `serial_ext.py` hands to esp_idf_monitor),
  * remember what was flashed (`${CB_BUILD_ROOT}/flash.json`) so a later `monitor` can explain the
    "Checksum mismatch between flashed and built applications" warning instead of leaving the
    operator guessing (it is cosmetic: different build root, or an ELF rebuilt after the flash).
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from .context import Ctx

FLASH_RECORD = "flash.json"


@dataclass(frozen=True)
class ElfInfo:
    path: Path
    sha256: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def app_elf(build_dir: Path) -> Path | None:
    """The ELF idf.py monitor compares against (see IDF serial_ext.py: as build_dir/app_elf)."""
    description = build_dir / "project_description.json"
    if description.is_file():
        try:
            data = json.loads(description.read_text(encoding="utf-8", errors="replace"))
            name = data.get("app_elf")
            if isinstance(name, str) and name:
                candidate = build_dir / name
                if candidate.is_file():
                    return candidate
        except (OSError, json.JSONDecodeError):
            pass
    candidates = sorted(build_dir.glob("*.elf"))
    return candidates[0] if candidates else None


def elf_info(build_dir: Path) -> ElfInfo | None:
    path = app_elf(build_dir)
    if path is None:
        return None
    return ElfInfo(path, sha256_file(path))


def flash_record_path(ctx: Ctx) -> Path:
    return ctx.build_root / FLASH_RECORD


def write_flash_record(ctx: Ctx, *, build_dir: Path, elf: ElfInfo | None, port: str, target: str) -> Path:
    path = flash_record_path(ctx)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "target": target,
        "build_dir": str(build_dir),
        "elf": str(elf.path) if elf else None,
        "sha256": elf.sha256 if elf else None,
        "port": port,
        "at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def read_flash_record(ctx: Ctx) -> dict | None:
    path = flash_record_path(ctx)
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8", errors="replace"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def monitor_mismatch_note(ctx: Ctx, *, target: str, elf: ElfInfo | None) -> str | None:
    """One-line explanation of the idf_monitor checksum warning, or None when it cannot apply."""
    record = read_flash_record(ctx)
    if record is None or elf is None:
        return None
    flashed_hash = record.get("sha256")
    flashed_target = record.get("target")
    if not isinstance(flashed_hash, str) or not flashed_hash:
        return None
    if flashed_target and flashed_target != target:
        return (
            f"cb: note: the last flash recorded by ./cb was target '{flashed_target}', "
            f"monitor is decoding '{target}' — panic backtraces may not match the running image."
        )
    if flashed_hash != elf.sha256:
        return (
            "cb: idf_monitor's 'Checksum mismatch' warning is cosmetic here: the ELF in this build "
            f"root was rebuilt after the last flash ({elf.sha256[:12]}… vs flashed {flashed_hash[:12]}…), "
            "or the flash used a different CB_BUILD_ROOT. The device still runs the flashed image; "
            "re-flash (./cb flash --target " + target + ") to silence it."
        )
    return None
