"""Environment / toolchain checks (the `doctor` command and the `verify` doctor step).

Peer-owned files that have not landed yet are reported as WARN, never as a fatal environment
problem: verify's build steps are what enforce their presence (CONTRACT R6).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from . import context as c
from .context import Ctx, arduino_cli, find_idf_activate
from .ports import list_ports
from .proc import Runner

PASS = "PASS"
WARN = "WARN"
FAIL = "FAIL"

IDF_EXPECTED_PREFIX = "ESP-IDF v6.1"
ARD_CORE_ID = "esp32:esp32"
ARD_CORE_VERSION = "3.3.12"
ARD_LIB = "lvgl"
MIN_DISK_GB = 6.0


@dataclass(frozen=True)
class Check:
    id: str
    status: str
    detail: str
    fatal: bool = False


def _py_version_ok() -> tuple[bool, str]:
    version = sys.version.split()[0]
    return (sys.version_info >= (3, 10), f"python {version}")


def _idf_details(runner: Runner) -> tuple[str, str]:
    script = find_idf_activate()
    if script is None:
        return (FAIL, "activation script not found ($CB_IDF_ACTIVATE / $IDF_PATH / ~/.espressif)")
    try:
        version = runner.idf_version()
    except Exception as exc:  # noqa: BLE001 - doctor must never crash
        return (FAIL, f"{script.name}: {exc}")
    if IDF_EXPECTED_PREFIX not in version:
        return (FAIL, f"{script.name}: idf.py reports {version!r}, expected {IDF_EXPECTED_PREFIX}")
    return (PASS, f"{version} via {script}")


def _arduino_details(runner: Runner) -> tuple[str, str]:
    cli = arduino_cli()
    if cli is None:
        return (WARN, "arduino-cli not found (tier-2 path; set $CB_ARDUINO_CLI)")
    result = runner.run([str(cli), "version"], timeout=60)
    if not result.ok:
        return (WARN, f"{cli}: {result.tail(2)}")
    version = result.out.strip().splitlines()[0] if result.out.strip() else "unknown"
    core = runner.run([str(cli), "core", "list"], timeout=60)
    core_line = ""
    if core.out:
        for line in core.out.splitlines():
            if line.startswith(ARD_CORE_ID):
                core_line = " ".join(line.split())
                break
    if ARD_CORE_VERSION not in core_line:
        return (WARN, f"{version}; core {ARD_CORE_ID} {ARD_CORE_VERSION} not found")
    libs = runner.run([str(cli), "lib", "list"], timeout=60)
    has_lvgl = any(line.split() and line.split()[0].lower() == ARD_LIB for line in libs.out.splitlines())
    return (PASS, f"{version}; {ARD_CORE_ID} {ARD_CORE_VERSION}; {ARD_LIB} {'installed' if has_lvgl else 'NOT installed (tier-2)'}")


def _disk_details(ctx: Ctx) -> tuple[str, str]:
    try:
        ctx.build_root.mkdir(parents=True, exist_ok=True)
        usage = shutil.disk_usage(ctx.build_root)
    except OSError as exc:
        return (WARN, f"cannot stat {ctx.build_root}: {exc}")
    free_gb = usage.free / (1024 ** 3)
    status = PASS if free_gb >= MIN_DISK_GB else WARN
    return (status, f"{free_gb:.1f} GiB free at {ctx.build_root}")


def _layout_checks(ctx: Ctx) -> list[Check]:
    checks: list[Check] = []

    def present(rel: str, owner: str) -> Check:
        path = ctx.path(rel)
        if path.exists():
            return Check(f"layout:{rel}", PASS, "present")
        return Check(f"layout:{rel}", WARN, f"missing (owner: {owner})")

    checks.append(present(c.ROOT_APP_CMAKE, "glm53"))
    checks.append(present(c.ROOT_APP_MAIN, "glm53"))
    checks.append(present(c.BSP_DIR, "mimo26-pro"))
    checks.append(present(c.BSP_PINS, "kimi-k31-pro"))
    checks.append(present(c.BOARD_DEFAULTS, "mimo26-pro"))
    checks.append(present(c.PARTITIONS_CSV, "mimo26-pro"))
    checks.append(present(c.HOST_TESTS_MAKEFILE, "kimi-k31-pro"))
    tools = ctx.path("tools/cbchecks")
    checks.append(
        Check("layout:tools/cbchecks", PASS if tools.is_dir() else FAIL, str(tools))
    )
    return checks


def _deps_warmed(ctx: Ctx) -> Check:
    manifest = ctx.path(c.BSP_DIR) / "idf_component.yml"
    if not manifest.is_file():
        # Item 1 ships no managed components (CONTRACT R5/R12): absence is the expected state.
        return Check("deps-warmed", PASS, "no idf_component.yml yet (item-1 choice: no managed deps)")
    text = manifest.read_text(encoding="utf-8", errors="replace")
    if "lvgl/lvgl" not in text and "esp_lvgl_port" not in text:
        return Check("deps-warmed", PASS, "no managed component pins yet")
    if ctx.offline:
        return Check("deps-warmed", PASS, "offline mode: fetch commands are forbidden")
    locks = list(ctx.path("examples").glob("*/dependencies.lock")) if ctx.path("examples").is_dir() else []
    app_lock = ctx.path("dependencies.lock")
    warmed = app_lock.is_file() or bool(locks)
    if warmed:
        return Check("deps-warmed", PASS, "dependencies.lock present")
    return Check("deps-warmed", WARN, "managed pins declared but no dependencies.lock yet — run './cb deps' once")


def _ports_check(ctx: Ctx) -> Check:
    ports = list_ports()
    if os.environ.get("CB_FORCE_NO_PORTS", "").strip() in ("1", "true", "yes", "on"):
        return Check("ports", WARN, "CB_FORCE_NO_PORTS=1 (verify mode): board checks forced offline")
    if not ports:
        return Check("ports", WARN, "none found — expected when no board is attached")
    return Check("ports", PASS, ", ".join(ports))


def run_doctor(ctx: Ctx, runner: Runner | None = None) -> list[Check]:
    runner = runner or Runner(ctx)
    checks: list[Check] = []

    py_ok, py_detail = _py_version_ok()
    checks.append(Check("python", PASS if py_ok else FAIL, py_detail, fatal=True))

    git = shutil.which("git")
    if git is None:
        checks.append(Check("git", FAIL, "git not found", fatal=True))
        branch = ""
    else:
        res = runner.run([git, "rev-parse", "--abbrev-ref", "HEAD"], cwd=ctx.root, timeout=30)
        branch = res.out.strip() or "(unknown)"
        checks.append(Check("git", PASS if res.ok else WARN, f"branch {branch}"))

    if branch and branch != "toolkit":
        checks.append(Check("branch", WARN, f"on {branch!r}, expected 'toolkit'"))

    idf_status, idf_detail = _idf_details(runner)
    checks.append(Check("idf", idf_status, idf_detail, fatal=True))

    if ctx.offline:
        checks.append(Check("network", PASS, "offline mode: no fetch commands will run"))
    else:
        checks.append(Check("network", PASS, "not probed (fetching only via './cb deps')"))

    ard_status, ard_detail = _arduino_details(runner)
    checks.append(Check("arduino-cli", ard_status, ard_detail))

    disk_status, disk_detail = _disk_details(ctx)
    checks.append(Check("disk", disk_status, disk_detail))
    checks.append(_ports_check(ctx))
    checks.append(Check("build-root", PASS, str(ctx.build_root)))
    checks.extend(_layout_checks(ctx))
    checks.append(_deps_warmed(ctx))
    return checks


def doctor_ok(checks: list[Check]) -> bool:
    return not any(chk.fatal and chk.status == FAIL for chk in checks)


def render(checks: list[Check]) -> str:
    width = max((len(c.id) for c in checks), default=4)
    lines = []
    for chk in checks:
        lines.append(f"[{chk.status}] {chk.id.ljust(width)}  {chk.detail}")
    return "\n".join(lines)
