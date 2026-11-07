"""The `verify` gate: stable step ids, fixed order, PASS/FAIL/SKIP, report at the end.

Frozen order (CONTRACT):
  doctor, host-tests, pin-drift, audit-static, build:app, build:<example>…, sdkconfig-assert,
  arduino:<sketch>…, guards-no-board, report

Ruling R6: for item 1 the host tests, the root app and 01_lcd_hello are required (missing → FAIL);
SKIP is allowed only for pin-drift and arduino:* (no arduino/ tree yet) and for --offline
sub-checks when unshare is unavailable. Ruling R8: --offline uses unshare, never
IDF_COMPONENT_MANAGER.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Sequence

from . import build as builder
from . import doctor, report, sizes, staticchecks
from .context import HOST_TESTS_MAKEFILE, Ctx
from .errors import CODE_FAIL, CODE_OK, CODE_USAGE, CbError
from .proc import Runner
from .targets import Target, app_target, build_dir, discover, idf_examples

PASS, FAIL, SKIP = "PASS", "FAIL", "SKIP"

# Item-1 required targets (R6): missing => FAIL, never SKIP.
REQUIRED_EXAMPLES = ("01_lcd_hello",)

OFFLINE_UNAVAILABLE = "offline namespace (unshare -rn) unavailable — NOT proven offline"


@dataclass
class StepResult:
    id: str
    status: str
    detail: str
    duration_ms: int = 0
    extra: dict = field(default_factory=dict)


class StepRunner:
    def __init__(self, ctx: Ctx, runner: Runner):
        self.ctx = ctx
        self.runner = runner
        self.results: list[StepResult] = []

    def step(self, step_id: str, fn: Callable[[], tuple[str, str, dict] | tuple[str, str]]) -> StepResult:
        start = int(time.monotonic() * 1000)
        try:
            outcome = fn()
            status, detail = outcome[0], outcome[1]
            extra = outcome[2] if len(outcome) > 2 else {}
        except CbError as exc:
            status, detail, extra = FAIL, exc.message, {}
        except Exception as exc:  # noqa: BLE001 - a step must never abort the gate
            status, detail, extra = FAIL, f"{type(exc).__name__}: {exc}", {}
        duration = int(time.monotonic() * 1000) - start
        result = StepResult(step_id, status, detail, duration, extra)
        self.results.append(result)
        print(f"[{result.status}] {result.id} {result.detail}", flush=True)
        return result

    def skip(self, step_id: str, detail: str) -> StepResult:
        result = StepResult(step_id, SKIP, detail, 0, {})
        self.results.append(result)
        print(f"[{SKIP}] {step_id} {detail}", flush=True)
        return result


def _host_tests(ctx: Ctx, runner: Runner) -> tuple[str, str, dict]:
    makefile = ctx.path(HOST_TESTS_MAKEFILE)
    if not makefile.is_file():
        return FAIL, f"{HOST_TESTS_MAKEFILE} missing (owner: kimi-k31-pro)", {"missing": HOST_TESTS_MAKEFILE}
    cwd = ctx.path("tests/host")
    make = os.environ.get("CB_MAKE", "make")
    result = runner.run_make([make, "test"], cwd=cwd, timeout=900.0)
    detail = "host tests passed"
    if not result.ok:
        detail = f"host tests failed: {result.tail(8)}"
    extra = {"log": str(ctx.log_path("host-tests")), "code": result.code}
    ctx.log_path("host-tests").parent.mkdir(parents=True, exist_ok=True)
    ctx.log_path("host-tests").write_text((result.out or "") + (result.err or ""), encoding="utf-8")
    return (PASS if result.ok else FAIL), detail, extra


def _guards(ctx: Ctx, runner: Runner) -> tuple[str, str, dict]:
    """R6: prove flash/monitor exit 2 and print the recovery hint, without hardware."""
    tree = ctx.root
    env = dict(os.environ)
    env["CB_FORCE_NO_PORTS"] = "1"
    env.setdefault("CB_BUILD_ROOT", os.environ.get("CB_BUILD_ROOT", str(ctx.build_root)))
    checks: dict[str, int] = {}
    problems: list[str] = []
    for command in ("flash", "monitor"):
        proc = runner.run(
            [sys.executable, str(tree / "tools" / "cb.py"), command],
            cwd=tree,
            env_extra={"CB_FORCE_NO_PORTS": "1"},
            timeout=120.0,
        )
        checks[command] = proc.code
        if proc.code != 2:
            problems.append(f"{command} exited {proc.code}, expected 2")
            continue
        text = (proc.err or "") + (proc.out or "")
        if "BOOT" not in text or "RESET" not in text:
            problems.append(f"{command} exited 2 but the message lacks the BOOT+RESET hint")
    if problems:
        return FAIL, "; ".join(problems), checks
    return PASS, "flash+monitor exit 2 with the recovery hint (CB_FORCE_NO_PORTS=1)", checks


def _offline_gate(ctx: Ctx, runner: Runner) -> bool:
    return (not ctx.offline) or runner.offline_available()


def verify(ctx: Ctx, runner: Runner, *, fast: bool = False) -> tuple[int, list[StepResult]]:
    steps = StepRunner(ctx, runner)
    meta = report.meta(ctx, runner, mode={"fast": fast, "offline": ctx.offline, "no_arduino": ctx.no_arduino})

    # 1. doctor ---------------------------------------------------------------
    doctor_failed = False

    def _doctor() -> tuple[str, str, dict]:
        nonlocal doctor_failed
        checks = doctor.run_doctor(ctx, runner)
        ok = doctor.doctor_ok(checks)
        doctor_failed = not ok
        detail = "all fatal checks passed"
        if not ok:
            fatal = [f"{c.id}: {c.detail}" for c in checks if c.fatal and c.status == doctor.FAIL]
            detail = "; ".join(fatal)
        warns = [c.id for c in checks if c.status == doctor.WARN]
        if warns:
            detail += f" (warnings: {', '.join(warns)})"
        return (PASS if ok else FAIL), detail, {"checks": [c.__dict__ for c in checks]}

    steps.step("doctor", _doctor)

    # 2. host tests -----------------------------------------------------------
    steps.step("host-tests", lambda: _host_tests(ctx, runner))

    # 3. pin drift ------------------------------------------------------------
    def _pin_drift() -> tuple[str, str, dict]:
        status, detail = staticchecks.pin_drift(ctx)
        return status, detail, {}

    steps.step("pin-drift", _pin_drift)

    # 4. static audit ---------------------------------------------------------
    def _audit() -> tuple[str, str, dict]:
        findings = staticchecks.scan_forbidden(ctx) + staticchecks.scan_abs_paths(ctx)
        if findings:
            sample = "; ".join(f.render() for f in findings[:5])
            more = f" (+{len(findings) - 5} more)" if len(findings) > 5 else ""
            return FAIL, f"{len(findings)} finding(s): {sample}{more}", {
                "findings": [f.render() for f in findings]
            }
        return PASS, "no forbidden constructs, no absolute home paths", {}

    steps.step("audit-static", _audit)

    # 5..6. builds ------------------------------------------------------------
    idf_targets: list[Target] = []
    offline_skipped = 0
    app = app_target(ctx)
    if fast:
        steps.skip("build:app", "fast mode (builds skipped)")
    elif app is None:
        steps.step("build:app", lambda: (FAIL, "root project missing: CMakeLists.txt + main/ (owner: glm53)", {}))
    elif not _offline_gate(ctx, runner):
        steps.skip("build:app", OFFLINE_UNAVAILABLE)
        offline_skipped += 1
    else:
        steps.step("build:app", lambda: _do_build(ctx, runner, app))
        if steps.results[-1].status == PASS:
            idf_targets.append(app)

    examples = idf_examples(ctx)
    present = {t.name for t in examples}
    for name in REQUIRED_EXAMPLES:
        if name not in present:
            steps.step(
                f"build:{name}",
                lambda name=name: (FAIL, f"examples/{name} missing (CMakeLists.txt not found)", {}),
            )
    for target in examples:
        if fast:
            steps.skip(f"build:{target.name}", "fast mode (builds skipped)")
            continue
        if not _offline_gate(ctx, runner):
            steps.skip(f"build:{target.name}", OFFLINE_UNAVAILABLE)
            offline_skipped += 1
            continue
        result = steps.step(f"build:{target.name}", lambda t=target: _do_build(ctx, runner, t))
        if result.status == PASS:
            idf_targets.append(target)

    # 7. sdkconfig + partition assertions ------------------------------------
    def _assert() -> tuple[str, str, dict]:
        if not idf_targets:
            return FAIL, "no IDF target was built, nothing to assert", {}
        problems: list[str] = []
        details: list[dict] = []
        for target in idf_targets:
            ok, detail, extra = builder.assert_sdkconfig(ctx, target, build_dir(ctx, target))
            details.append({"target": target.name, "ok": ok, "detail": detail, **extra})
            if not ok:
                problems.append(detail)
        if problems:
            return FAIL, "; ".join(problems), {"targets": details}
        return PASS, f"{len(idf_targets)} target(s): keys + partition table verified", {"targets": details}

    if fast:
        steps.skip("sdkconfig-assert", "fast mode (builds skipped)")
    elif offline_skipped:
        steps.skip("sdkconfig-assert", OFFLINE_UNAVAILABLE)
    else:
        steps.step("sdkconfig-assert", _assert)

    # 8. Arduino sketches (SKIP is allowed in item 1) -------------------------
    if ctx.no_arduino:
        pass
    else:
        sketches = [t for t in discover(ctx) if t.kind == "arduino"]
        if not sketches:
            steps.skip("arduino:*", "no arduino/ sketches yet (item 7)")
        for sketch in sketches:
            if fast:
                steps.skip(f"arduino:{sketch.name}", "fast mode (builds skipped)")
            elif not _offline_gate(ctx, runner):
                steps.skip(f"arduino:{sketch.name}", OFFLINE_UNAVAILABLE)
            else:
                steps.step(f"arduino:{sketch.name}", lambda t=sketch: _do_build(ctx, runner, t))

    # 9. no-board guards ------------------------------------------------------
    steps.step("guards-no-board", lambda: _guards(ctx, runner))

    # 10. report --------------------------------------------------------------
    def _report() -> tuple[str, str, dict]:
        md, js = report.write_report(ctx, steps.results, meta)
        return PASS, f"{md} + {js}", {"report_md": str(md), "report_json": str(js)}

    steps.step("report", _report)

    statuses = [r.status for r in steps.results]
    if doctor_failed:
        return CODE_USAGE, steps.results
    if FAIL in statuses:
        return CODE_FAIL, steps.results
    return CODE_OK, steps.results


def _do_build(ctx: Ctx, runner: Runner, target: Target) -> tuple[str, str, dict]:
    outcome = builder.build_target(ctx, runner, target)
    extra = dict(outcome.extra)
    if outcome.ok:
        extra["sizes"] = sizes.sizes_for(ctx, runner, target, outcome.build_dir)
    return (PASS if outcome.ok else FAIL), outcome.detail, extra
