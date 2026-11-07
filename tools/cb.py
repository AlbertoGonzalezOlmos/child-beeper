#!/usr/bin/env python3
"""./cb — Child Beeper toolkit CLI (owner: deepseek-flash).

Frozen surface (CONTRACT.md):
  doctor [--json] [--offline] | deps [--offline] | list | build [TARGET...|--all] | test |
  verify [--fast] [--no-arduino] [--offline] | ports [--json] | flash [--port P] [--target T] |
  monitor [--port P] | selftest [--port P] | arduino [SKETCH...] [--install-libs] |
  arduino-install | report | clean [TARGET|--all]

Exit codes: 0 ok, 1 build/test failure, 2 board/port absent, 3 usage or environment problem.
Standard library only.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cbchecks import build as builder  # noqa: E402
from cbchecks import doctor as doctor_mod  # noqa: E402
from cbchecks import ports as ports_mod  # noqa: E402
from cbchecks import report as report_mod  # noqa: E402
from cbchecks import targets as targets_mod  # noqa: E402
from cbchecks import verify as verify_mod  # noqa: E402
from cbchecks.context import (  # noqa: E402
    ARDUINO_DIR,
    HOST_TESTS_MAKEFILE,
    PARTITIONS_CSV,
    Ctx,
    arduino_cli,
    load_ctx,
)
from cbchecks.errors import (  # noqa: E402
    CODE_FAIL,
    CODE_OK,
    CODE_USAGE,
    BoardError,
    CbError,
    EnvError,
    UsageError,
)
from cbchecks.proc import Runner  # noqa: E402

PROG = "cb"


def _print(msg: str = "", *, err: bool = False) -> None:
    print(msg, file=sys.stderr if err else sys.stdout, flush=True)


# --------------------------------------------------------------------------- doctor
def cmd_doctor(args: argparse.Namespace) -> int:
    ctx = load_ctx(offline=args.offline)
    checks = doctor_mod.run_doctor(ctx, Runner(ctx))
    if args.json:
        _print(json.dumps({"build_root": str(ctx.build_root), "checks": [c.__dict__ for c in checks]}, indent=2))
    else:
        _print(doctor_mod.render(checks))
        fatals = [c for c in checks if c.fatal and c.status == doctor_mod.FAIL]
        if fatals:
            _print("\ncb: environment problems above must be fixed first.", err=True)
    return CODE_OK if doctor_mod.doctor_ok(checks) else CODE_USAGE


# ----------------------------------------------------------------------------- deps
def cmd_deps(args: argparse.Namespace) -> int:
    ctx = load_ctx(offline=args.offline)
    runner = Runner(ctx)
    if ctx.offline:
        # Never fetch while offline: only report what is already on disk.
        locks = [ctx.path("dependencies.lock")] + (
            sorted(ctx.path("examples").glob("*/dependencies.lock")) if ctx.path("examples").is_dir() else []
        )
        existing = [str(p) for p in locks if p.is_file()]
        if existing:
            _print("offline: dependency locks present")
            for path in existing:
                _print(f"  {path}")
            return CODE_OK
        _print("offline: no dependencies.lock found — run './cb deps' once with network access", err=True)
        return CODE_FAIL

    idf_targets = [t for t in targets_mod.discover(ctx) if t.is_idf]
    if not idf_targets:
        _print("cb: no IDF project discovered yet (root app / examples) — nothing to resolve", err=True)
        return CODE_USAGE
    failures: list[str] = []
    for target in idf_targets:
        bdir = targets_mod.build_dir(ctx, target)
        bdir.mkdir(parents=True, exist_ok=True)
        builder._preseed_sdkconfig(ctx, target, bdir)
        result = runner.run_idf(
            [
                "-C", str(target.path),
                "-B", str(bdir),
                f"-DSDKCONFIG={bdir / 'sdkconfig'}",
                "-DIDF_TARGET=esp32s3",
                "reconfigure",
            ],
            cwd=ctx.root,
            timeout=1800.0,
        )
        partition_bin = bdir / "partition_table" / "partition-table.bin"
        # `idf.py reconfigure` can exit 0 while the partition step tracebacks (measured):
        # assert the artefact instead of trusting the status code.
        if not result.ok or not partition_bin.is_file():
            failures.append(f"{target.name}: reconfigure did not produce {partition_bin}")
            _print(f"[FAIL] deps {target.name}: {(result.tail(6) or 'no partition table').strip()}", err=True)
        else:
            _print(f"[PASS] deps {target.name}: {bdir}")
    if failures:
        _print("cb: dependency preparation failed for: " + "; ".join(failures), err=True)
        return CODE_FAIL
    _print("deps: component cache warmed; dependencies.lock written by the projects")
    return CODE_OK


# ----------------------------------------------------------------------------- list
def cmd_list(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    found = targets_mod.discover(ctx)
    if not found:
        _print("no targets discovered yet (root app / examples / arduino absent)")
        return CODE_OK
    width = max(len(t.name) for t in found)
    for target in found:
        bdir = targets_mod.build_dir(ctx, target)
        _print(f"{target.kind:8s} {target.name.ljust(width)}  {target.path}  ->  {bdir}")
    return CODE_OK


# ---------------------------------------------------------------------------- build
def cmd_build(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    names = ["all"] if args.all else list(args.targets or [])
    chosen = targets_mod.resolve(ctx, names)
    if not chosen:
        raise UsageError("no targets to build")
    runner = Runner(ctx)
    failed: list[str] = []
    for target in chosen:
        _print(f"cb: building {target.name} …")
        outcome = builder.build_target(ctx, runner, target)
        status = "PASS" if outcome.ok else "FAIL"
        _print(f"[{status}] build:{target.name} {outcome.detail}")
        if not outcome.ok:
            failed.append(target.name)
    if failed:
        _print("cb: build failed for: " + ", ".join(failed), err=True)
        return CODE_FAIL
    return CODE_OK


# ----------------------------------------------------------------------------- test
def cmd_test(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    if not ctx.path(HOST_TESTS_MAKEFILE).is_file():
        raise EnvError(
            f"{HOST_TESTS_MAKEFILE} not found (owner: kimi-k31-pro) — host tests are not landable yet"
        )
    runner = Runner(ctx)
    make = os.environ.get("CB_MAKE", "make")
    result = runner.run_make([make, "test"], cwd=ctx.path("tests/host"), timeout=900.0)
    sys.stdout.write(result.out)
    sys.stderr.write(result.err)
    return CODE_OK if result.ok else CODE_FAIL


# --------------------------------------------------------------------------- verify
def cmd_verify(args: argparse.Namespace) -> int:
    ctx = load_ctx(offline=args.offline, no_arduino=args.no_arduino)
    runner = Runner(ctx)
    code, results = verify_mod.verify(ctx, runner, fast=args.fast)
    totals: dict[str, int] = {}
    for result in results:
        totals[result.status] = totals.get(result.status, 0) + 1
    md_path, json_path = report_mod.report_paths(ctx)
    _print("")
    _print(f"verify: {totals.get('PASS', 0)} pass, {totals.get('FAIL', 0)} fail, {totals.get('SKIP', 0)} skip")
    _print(f"report: {md_path} + {json_path}")
    if code == CODE_USAGE:
        _print("cb: verify failed in the environment/doctor step (exit 3)", err=True)
    elif code == CODE_FAIL:
        failed = [r.id for r in results if r.status == verify_mod.FAIL]
        _print("cb: verify failed: " + ", ".join(failed), err=True)
    return code


# ---------------------------------------------------------------------------- ports
def cmd_ports(args: argparse.Namespace) -> int:
    found = ports_mod.list_ports()
    if args.json:
        _print(json.dumps({"ports": found, "forced_no_ports": ports_mod.forced_no_ports()}, indent=2))
    elif not found:
        _print("no serial ports found (expected when no board is attached)")
    else:
        for port in found:
            _print(port)
    return CODE_OK


# ---------------------------------------------------------------------------- flash
def cmd_flash(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    port = ports_mod.guard_port(args.port)  # guard BEFORE building (contract)
    target = targets_mod.resolve(ctx, [args.target or targets_mod.APP_NAME])[0]
    if not target.is_idf:
        raise UsageError(f"cannot flash {target.name}: not an IDF target")
    runner = Runner(ctx)
    outcome = builder.build_target(ctx, runner, target)
    if not outcome.ok:
        raise CbError(f"build failed before flashing {target.name}: {outcome.detail}")
    result = runner.run_idf(
        [
            "-C", str(target.path),
            "-B", str(outcome.build_dir),
            f"-DSDKCONFIG={outcome.build_dir / 'sdkconfig'}",
            "-p", port,
            "flash",
        ],
        cwd=ctx.root,
        timeout=1800.0,
    )
    _print(result.out)
    _print(result.err, err=True)
    return CODE_OK if result.ok else CODE_FAIL


# -------------------------------------------------------------------------- monitor
def cmd_monitor(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    port = ports_mod.guard_port(args.port)
    target = targets_mod.resolve(ctx, [targets_mod.APP_NAME])[0]
    _print("cb: monitoring — press Ctrl-] to quit")
    runner = Runner(ctx)
    result = runner.run_idf(
        [
            "-C", str(target.path),
            "-B", str(targets_mod.build_dir(ctx, target)),
            "-p", port,
            "monitor",
        ],
        cwd=ctx.root,
        timeout=float(os.environ.get("CB_MONITOR_TIMEOUT", "86400")),
    )
    _print(result.out)
    _print(result.err, err=True)
    return CODE_OK if result.ok else CODE_FAIL


# ------------------------------------------------------------------------- selftest
def _parse_selftest(lines: list[str]) -> tuple[bool, str]:
    items = 0
    result_line = ""
    failures = 0
    for line in lines:
        stripped = line.strip()
        if "ITEM:" in stripped:
            items += 1
            if "FAIL" in stripped.upper():
                failures += 1
        if "RESULT:" in stripped:
            result_line = stripped
    if not result_line:
        return False, f"no RESULT: line within timeout (saw {items} ITEM lines)"
    if failures or "FAIL" in result_line.upper():
        return False, f"{failures} failing item(s); {result_line}"
    return True, result_line


def cmd_selftest(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    port = ports_mod.guard_port(args.port)  # guard BEFORE building/flashing
    available = {t.name for t in targets_mod.discover(ctx)}
    name = "06_dashboard" if "06_dashboard" in available else targets_mod.APP_NAME
    target = targets_mod.resolve(ctx, [name])[0]
    runner = Runner(ctx)
    outcome = builder.build_target(ctx, runner, target)
    if not outcome.ok:
        raise CbError(f"build failed before selftest: {outcome.detail}")
    flash = runner.run_idf(
        ["-C", str(target.path), "-B", str(outcome.build_dir), "-p", port, "flash"],
        cwd=ctx.root,
        timeout=1800.0,
    )
    if not flash.ok:
        _print(flash.tail(20), err=True)
        return CODE_FAIL
    timeout = float(os.environ.get("CB_SELFTEST_TIMEOUT", "90"))
    lines = list(
        runner.monitor_stream(
            ["-C", str(target.path), "-B", str(outcome.build_dir), "-p", port, "monitor"],
            cwd=ctx.root,
            timeout=timeout,
            stop_marker="RESULT:",
        )
    )
    ok, detail = _parse_selftest(lines)
    _print(("[PASS] " if ok else "[FAIL] ") + "selftest " + detail)
    return CODE_OK if ok else CODE_FAIL


# -------------------------------------------------------------------------- arduino
def cmd_arduino(args: argparse.Namespace) -> int:
    ctx = load_ctx(no_arduino=True)
    sketches = [t for t in targets_mod.discover(load_ctx()) if t.kind == "arduino"]
    if args.install_libs:
        if ctx.offline:
            raise EnvError("--install-libs needs network access; refusing in offline mode")
        cli = arduino_cli()
        if cli is None:
            raise EnvError("arduino-cli not found (set $CB_ARDUINO_CLI)")
        result = Runner(ctx).run([str(cli), "lib", "install", "lvgl@9.5.0"], timeout=900.0)
        _print(result.out)
        if not result.ok:
            _print(result.err, err=True)
            return CODE_FAIL
    if not sketches:
        _print("no Arduino sketches yet (arduino/ tree lands in item 7)")
        return CODE_OK
    if args.sketches:
        wanted = set(args.sketches)
        unknown = wanted - {t.name for t in sketches}
        if unknown:
            raise UsageError(f"unknown sketch(es): {', '.join(sorted(unknown))}")
        sketches = [t for t in sketches if t.name in wanted]
    runner = Runner(ctx)
    failed: list[str] = []
    for sketch in sketches:
        outcome = builder.build_arduino(ctx, runner, sketch)
        _print(f"[{'PASS' if outcome.ok else 'FAIL'}] arduino:{sketch.name} {outcome.detail}")
        if not outcome.ok:
            failed.append(sketch.name)
    return CODE_FAIL if failed else CODE_OK


def cmd_arduino_install(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    libs_src = ctx.path(ARDUINO_DIR) / "libraries"
    libraries = sorted(p for p in libs_src.glob("*") if p.is_dir()) if libs_src.is_dir() else []
    if not libraries:
        raise EnvError("no Arduino libraries to install yet (arduino/ tree lands in item 7)")
    sketchbook = Path(os.environ.get("ARDUINO_SKETCHBOOK", str(Path.home() / "Arduino")))
    target_dir = sketchbook / "libraries"
    target_dir.mkdir(parents=True, exist_ok=True)
    for lib in libraries:
        dest = target_dir / lib.name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(lib, dest)
        _print(f"installed {lib.name} -> {dest}")
    return CODE_OK


# --------------------------------------------------------------------------- report
def cmd_report(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    md_path, _ = report_mod.report_paths(ctx)
    if not md_path.is_file():
        raise EnvError(f"{md_path} not found — run './cb verify' first")
    sys.stdout.write(md_path.read_text(encoding="utf-8"))
    return CODE_OK


# ---------------------------------------------------------------------------- clean
def cmd_clean(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    root = ctx.root.resolve()
    build_root = ctx.build_root.resolve()
    if root == build_root or build_root == Path(build_root.anchor or "/"):
        raise UsageError(f"refusing to clean {build_root}: not a project build directory")
    try:
        build_root.relative_to(root)
    except ValueError:
        raise UsageError(
            f"refusing to clean {build_root}: build root is outside the repository "
            "(set CB_BUILD_ROOT to a path inside the repo to clean it)"
        ) from None
    if args.all:
        if build_root.exists():
            shutil.rmtree(build_root)
            _print(f"removed {build_root}")
        return CODE_OK
    if not args.target:
        raise UsageError("clean needs a target or --all")
    target = targets_mod.resolve(ctx, [args.target])[0]
    bdir = targets_mod.build_dir(ctx, target)
    if bdir.exists():
        shutil.rmtree(bdir)
        _print(f"removed {bdir}")
    return CODE_OK


# ------------------------------------------------------------------------------ cli
def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog=PROG, description="Child Beeper toolkit CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("doctor", help="environment/toolchain/board checks")
    p.add_argument("--json", action="store_true")
    p.add_argument("--offline", action="store_true")
    p.set_defaults(func=cmd_doctor)

    p = sub.add_parser("deps", help="resolve/warm component dependencies")
    p.add_argument("--offline", action="store_true")
    p.set_defaults(func=cmd_deps)

    p = sub.add_parser("list", help="list targets discovered on disk")
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("build", help="build one or more targets")
    p.add_argument("targets", nargs="*")
    p.add_argument("--all", action="store_true")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("test", help="run host unit tests")
    p.set_defaults(func=cmd_test)

    p = sub.add_parser("verify", help="run the no-board verification gate")
    p.add_argument("--fast", action="store_true")
    p.add_argument("--no-arduino", action="store_true")
    p.add_argument("--offline", action="store_true")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("ports", help="list serial ports")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_ports)

    p = sub.add_parser("flash", help="flash a target (guarded)")
    p.add_argument("--port")
    p.add_argument("--target")
    p.set_defaults(func=cmd_flash)

    p = sub.add_parser("monitor", help="open the serial monitor (guarded)")
    p.add_argument("--port")
    p.set_defaults(func=cmd_monitor)

    p = sub.add_parser("selftest", help="flash the selftest and check the serial contract (guarded)")
    p.add_argument("--port")
    p.set_defaults(func=cmd_selftest)

    p = sub.add_parser("arduino", help="compile Arduino sketches")
    p.add_argument("sketches", nargs="*")
    p.add_argument("--install-libs", action="store_true")
    p.set_defaults(func=cmd_arduino)

    p = sub.add_parser("arduino-install", help="copy arduino/libraries into the IDE sketchbook")
    p.set_defaults(func=cmd_arduino_install)

    p = sub.add_parser("report", help="print the last verify report")
    p.set_defaults(func=cmd_report)

    p = sub.add_parser("clean", help="remove build output")
    p.add_argument("target", nargs="?")
    p.add_argument("--all", action="store_true")
    p.set_defaults(func=cmd_clean)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except BoardError as exc:
        _print(exc.message, err=True)
        return exc.code
    except CbError as exc:
        _print(f"cb: {exc.message}", err=True)
        return exc.code
    except KeyboardInterrupt:
        _print("cb: interrupted", err=True)
        return CODE_FAIL
    except BrokenPipeError:
        return CODE_OK


if __name__ == "__main__":
    sys.exit(main())
