"""Target discovery: root app, IDF examples, Arduino sketches, and their build dirs."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

from .context import ARDUINO_DIR, EXAMPLES_DIR, ROOT_APP_CMAKE, ROOT_APP_MAIN, Ctx
from .errors import UsageError

# Measured-buildable FQBN for this board (arduino-cli 1.4.1 / core esp32:esp32 3.3.12).
ARD_FQBN = (
    "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,"
    "USBMode=hwcdc,PartitionScheme=default"
)

APP_NAME = "app"


@dataclass(frozen=True)
class Target:
    kind: str  # "app" | "idf" | "arduino"
    name: str
    path: Path

    @property
    def is_idf(self) -> bool:
        return self.kind in ("app", "idf")


def app_target(ctx: Ctx) -> Target | None:
    cmake = ctx.path(ROOT_APP_CMAKE)
    if cmake.is_file() and ctx.path(ROOT_APP_MAIN).is_dir():
        return Target("app", APP_NAME, ctx.root)
    return None


def idf_examples(ctx: Ctx) -> list[Target]:
    base = ctx.path(EXAMPLES_DIR)
    if not base.is_dir():
        return []
    found = [
        Target("idf", child.name, child)
        for child in sorted(base.iterdir(), key=lambda p: p.name)
        if child.is_dir() and (child / "CMakeLists.txt").is_file()
    ]
    return found


def arduino_sketches(ctx: Ctx) -> list[Target]:
    base = ctx.path(ARDUINO_DIR)
    if not base.is_dir():
        return []
    found: list[Target] = []
    for parent in (base / "libraries", base / "sketches"):
        if not parent.is_dir():
            continue
        for sketch_dir in sorted(parent.rglob("*"), key=lambda p: str(p)):
            if not sketch_dir.is_dir():
                continue
            if not list(sketch_dir.glob("*.ino")):
                continue
            # only leaf sketch dirs (containing an .ino) count
            found.append(Target("arduino", sketch_dir.name, sketch_dir))
    # de-duplicate by name, keeping the first stable path
    seen: set[str] = set()
    unique: list[Target] = []
    for target in found:
        if target.name in seen:
            continue
        seen.add(target.name)
        unique.append(target)
    return unique


def discover(ctx: Ctx) -> list[Target]:
    targets: list[Target] = []
    app = app_target(ctx)
    if app is not None:
        targets.append(app)
    targets.extend(idf_examples(ctx))
    if not ctx.no_arduino:
        targets.extend(arduino_sketches(ctx))
    return targets


def build_dir(ctx: Ctx, target: Target) -> Path:
    if target.kind == "arduino":
        return ctx.build_root / "arduino" / target.name
    return ctx.target_build_dir(target.name)


def resolve(ctx: Ctx, names: Sequence[str], *, include_arduino: bool = True) -> list[Target]:
    available = discover(ctx) if include_arduino else [t for t in discover(ctx) if t.is_idf]
    if not names:
        app = app_target(ctx)
        if app is None:
            raise UsageError(
                "no target given and the root app does not exist yet "
                "(expected CMakeLists.txt + main/ at the repo root)"
            )
        return [app]
    by_name = {t.name: t for t in available}
    chosen: list[Target] = []
    unknown: list[str] = []
    for name in names:
        if name == "all":
            chosen.extend(available)
            continue
        target = by_name.get(name)
        if target is None:
            unknown.append(name)
        else:
            chosen.append(target)
    if unknown:
        valid = ", ".join(sorted(by_name)) or "(none discovered)"
        raise UsageError(f"unknown target(s): {', '.join(unknown)}; valid targets: {valid}")
    # de-duplicate while keeping order
    seen: set[str] = set()
    result: list[Target] = []
    for target in chosen:
        if target.name in seen:
            continue
        seen.add(target.name)
        result.append(target)
    return result
