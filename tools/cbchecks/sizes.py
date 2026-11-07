"""Artifact sizes: `idf.py size --format json` when it works, `.bin` size as the mandatory fallback.

Ruling R10: the JSON path is best-effort; the .bin fallback must always produce a number.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from .context import Ctx
from .proc import Result, Runner
from .targets import Target


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def primary_bin(build_dir: Path) -> Path | None:
    candidates = sorted(build_dir.glob("*.bin"))
    if not candidates:
        return None
    # prefer the biggest top-level image (the app), never partition-table/bootloader
    candidates = [p for p in candidates if p.name not in ("partition-table.bin", "bootloader.bin")]
    if not candidates:
        return None
    return max(candidates, key=lambda p: p.stat().st_size)


def artifacts(build_dir: Path) -> list[dict]:
    result: list[dict] = []
    binary = primary_bin(build_dir)
    if binary is not None:
        result.append(
            {"path": str(binary), "bytes": binary.stat().st_size, "sha256": _sha256(binary)}
        )
    elf = sorted(build_dir.glob("*.elf"))
    if elf:
        result.append({"path": str(elf[0]), "bytes": elf[0].stat().st_size, "sha256": _sha256(elf[0])})
    partition = build_dir / "partition_table" / "partition-table.bin"
    if partition.is_file():
        result.append({"path": str(partition), "bytes": partition.stat().st_size, "sha256": _sha256(partition)})
    return result


def sizes_for(ctx: Ctx, runner: Runner, target: Target, build_dir: Path) -> dict:
    binary = primary_bin(build_dir)
    info: dict = {
        "bin": str(binary) if binary else None,
        "bin_bytes": binary.stat().st_size if binary else None,
        "source": "bin",
        "fallback": True,
    }
    result: Result = runner.run_idf(
        ["-C", str(target.path), "-B", str(build_dir), "size", "--format", "json"],
        cwd=ctx.root,
        timeout=300.0,
    )
    if result.ok and result.out.strip():
        try:
            parsed = json.loads(result.out)
            info["source"] = "idf.py size --format json"
            info["fallback"] = False
            info["size_json"] = parsed
        except json.JSONDecodeError:
            info["size_error"] = "unparseable JSON from idf.py size"
    else:
        info["size_error"] = (result.err or result.out or "idf.py size failed").strip().splitlines()[-1:] or ["failed"]
    return info
