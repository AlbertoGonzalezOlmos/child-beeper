"""Dependency-pin assertion for `./cb verify` (ITEM2 ruling A9).

The component manager resolves from the manifests; the lock files record what a project actually
built with. Both must agree with the frozen pins, otherwise the display stack silently drifts
(lvgl 9.6 changes API, esp_lvgl_port 2.8 lacks `swap_bytes`, led_strip 2.x is broken on IDF 6).

Read-only and network-free: pure file parsing, so it also runs under --fast and --offline.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from . import miniyaml
from .context import BSP_DIR, Ctx

MANIFEST_RELPATH = f"{BSP_DIR}/idf_component.yml"
LOCK_NAME = "dependencies.lock"

EXPECTED_COMPONENTS: dict[str, str] = {
    "lvgl/lvgl": "9.5.0",
    "espressif/esp_lvgl_port": "2.9.0",
    "espressif/led_strip": "3.1.1",
}
EXPECTED_IDF = "6.1.0"

SKIP_DIRS = {".git", "build", ".cache", "managed_components", "__pycache__", "node_modules"}
RANGE_CHARS = ("^", "~", ">", "<", "*")


@dataclass(frozen=True)
class PinProblem:
    file: str
    subject: str
    expected: str
    actual: str | None
    kind: str  # "lock" | "manifest" | "missing" | "unparsable"

    def render(self) -> str:
        if self.kind == "missing":
            return f"{self.file}: missing (expected {self.subject} {self.expected}); run ./cb deps"
        if self.kind == "unparsable":
            return f"{self.file}: cannot parse ({self.subject})"
        got = self.actual if self.actual is not None else "absent"
        return f"{self.file}: {self.subject} is {got}, expected {self.expected}"


def _iter_locks(ctx: Ctx) -> list[Path]:
    found: list[Path] = []
    for path in sorted(ctx.root.rglob(LOCK_NAME)):
        rel_parts = path.relative_to(ctx.root).parts
        if any(part in SKIP_DIRS for part in rel_parts):
            continue
        found.append(path)
    return found


def expected_lock_projects(ctx: Ctx) -> list[Path]:
    """Every IDF project in the tree must ship a lock: the root app and every examples/* project."""
    projects: list[Path] = []
    if (ctx.root / "CMakeLists.txt").is_file():
        projects.append(ctx.root)
    examples = ctx.root / "examples"
    if examples.is_dir():
        for child in sorted(examples.iterdir(), key=lambda p: p.name):
            if child.is_dir() and (child / "CMakeLists.txt").is_file():
                projects.append(child)
    return projects


def parse_lock(path: Path) -> dict:
    return miniyaml.parse(path.read_text(encoding="utf-8", errors="replace"))


def lock_pins(path: Path) -> dict[str, str | None]:
    data = parse_lock(path)
    deps = data.get("dependencies") if isinstance(data, dict) else None
    if not isinstance(deps, dict):
        raise ValueError("no 'dependencies' map")
    pins: dict[str, str | None] = {}
    for name in (*EXPECTED_COMPONENTS, "idf"):
        node = deps.get(name)
        pins[name] = node.get("version") if isinstance(node, dict) else None
    return pins


def check_locks(ctx: Ctx) -> list[PinProblem]:
    problems: list[PinProblem] = []
    locks = _iter_locks(ctx)
    present = {path.parent for path in locks}
    for project in expected_lock_projects(ctx):
        if project not in present:
            rel = "dependencies.lock" if project == ctx.root else f"{project.relative_to(ctx.root)}/{LOCK_NAME}"
            problems.append(PinProblem(rel, "components", "present", None, "missing"))
    for path in locks:
        rel = path.relative_to(ctx.root).as_posix()
        try:
            pins = lock_pins(path)
        except (ValueError, OSError) as exc:
            problems.append(PinProblem(rel, f"yaml:{exc}", "readable", None, "unparsable"))
            continue
        for name, expected in {**EXPECTED_COMPONENTS, "idf": EXPECTED_IDF}.items():
            actual = pins.get(name)
            if actual != expected:
                problems.append(PinProblem(rel, name, expected, actual, "lock"))
    return problems


def manifest_pins(ctx: Ctx) -> dict[str, str]:
    path = ctx.path(MANIFEST_RELPATH)
    if not path.is_file():
        return {}
    data = miniyaml.parse(path.read_text(encoding="utf-8", errors="replace"))
    deps = data.get("dependencies") if isinstance(data, dict) else None
    if not isinstance(deps, dict):
        return {}
    return {str(key): str(value) for key, value in deps.items()}


def check_manifest(ctx: Ctx) -> list[PinProblem]:
    path = ctx.path(MANIFEST_RELPATH)
    if not path.is_file():
        return [PinProblem(MANIFEST_RELPATH, "manifest", "present", None, "missing")]
    try:
        pins = manifest_pins(ctx)
    except (ValueError, OSError) as exc:
        return [PinProblem(MANIFEST_RELPATH, f"yaml:{exc}", "readable", None, "unparsable")]
    problems: list[PinProblem] = []
    for name, expected in EXPECTED_COMPONENTS.items():
        actual = pins.get(name)
        if actual is None:
            problems.append(PinProblem(MANIFEST_RELPATH, name, expected, None, "manifest"))
        elif actual != expected:
            problems.append(PinProblem(MANIFEST_RELPATH, name, expected, actual, "manifest"))
    return problems


def check_pins(ctx: Ctx) -> tuple[bool, str, dict]:
    """Step body: never raises; the detail is a single line."""
    problems = check_locks(ctx) + check_manifest(ctx)
    locks = [path.relative_to(ctx.root).as_posix() for path in _iter_locks(ctx)]
    extra = {
        "locks": locks,
        "components": [{"name": name, "expected": version} for name, version in EXPECTED_COMPONENTS.items()],
        "idf_expected": EXPECTED_IDF,
        "manifest": MANIFEST_RELPATH,
        "problems": [p.render() for p in problems],
    }
    if problems:
        return False, "; ".join(p.render() for p in problems[:4]), extra
    resolved = ", ".join(f"{name} {version}" for name, version in EXPECTED_COMPONENTS.items())
    return True, f"{len(locks)} lock(s) + manifest: {resolved}, idf {EXPECTED_IDF}", extra
