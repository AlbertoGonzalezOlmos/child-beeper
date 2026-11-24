#!/usr/bin/env python3
"""./cb — Child Beeper toolkit CLI (owner: deepseek-flash).

Frozen surface (CONTRACT.md):
  doctor [--json] [--offline] | deps [--offline] | list | build [TARGET...|--all] | test |
  verify [--fast] [--no-arduino] [--offline] | ports [--json] | flash [--port P] [--target T] |
  monitor [--port P] | selftest [--port P] | arduino [SKETCH...] [--install-libs] |
  arduino --upload [SKETCH] [--port P] | arduino-install | report | clean [TARGET|--all]

Exit codes: 0 ok, 1 build/test failure, 2 board/port absent, 3 usage or environment problem.
Standard library only.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from cbchecks import binaries as binaries_mod  # noqa: E402
from cbchecks import build as builder  # noqa: E402
from cbchecks import arduino as arduino_mod  # noqa: E402
from cbchecks import doctor as doctor_mod  # noqa: E402
from cbchecks import ports as ports_mod  # noqa: E402
from cbchecks import report as report_mod  # noqa: E402
from cbchecks import targets as targets_mod  # noqa: E402
from cbchecks import verify as verify_mod  # noqa: E402
from cbchecks.context import (  # noqa: E402
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
        sdkconfig = bdir / "sdkconfig"
        # `idf.py reconfigure` can exit 0 while emitting a traceback (measured on a bad CSV path),
        # so success requires the generated sdkconfig as well as the exit code. The partition
        # binary is produced by the build, not by reconfigure: './cb verify' asserts it
        # (sdkconfig-assert) plus the two partition content lines.
        keys_ok = sdkconfig.is_file() and all(
            key in sdkconfig.read_text(encoding="utf-8", errors="replace")
            for key in ("CONFIG_PARTITION_TABLE_CUSTOM=y", "CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y")
        )
        if not result.ok or not keys_ok:
            failures.append(f"{target.name}: reconfigure did not produce a usable {sdkconfig}")
            _print(f"[FAIL] deps {target.name}: {(result.tail(6) or 'no sdkconfig').strip()}", err=True)
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
    if args.all:
        # CONTRACT: TARGET = "app" | example dir name. Arduino sketches are built by `./cb arduino`.
        chosen = [t for t in chosen if t.is_idf]
    if not chosen:
        raise UsageError("no IDF targets to build (root app / examples absent)")
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
    if result.ok:
        elf = binaries_mod.elf_info(outcome.build_dir)
        record = binaries_mod.write_flash_record(
            ctx, build_dir=outcome.build_dir, elf=elf, port=port, target=target.name
        )
        _print(
            f"cb: recorded flash: target {target.name} -> {record}"
            + (f" (elf sha256 {elf.sha256[:12]}…)" if elf else " (no ELF found in the build dir)")
        )
    return CODE_OK if result.ok else CODE_FAIL


# -------------------------------------------------------------------------- monitor
def cmd_monitor(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    port = ports_mod.guard_port(args.port)
    # RULED (item-2 review fixes): monitor must follow the flashed target, not always the root app —
    # panic backtrace decoding and idf_monitor's ELF hash check use this build dir's ELF.
    target = targets_mod.resolve(ctx, [args.target or targets_mod.APP_NAME])[0]
    if not target.is_idf:
        raise UsageError(f"cannot monitor {target.name}: not an IDF target")
    build_dir = targets_mod.build_dir(ctx, target)
    elf = binaries_mod.elf_info(build_dir)
    if not os.isatty(0):
        _print("cb: warning: stdin is not a terminal — Ctrl-] may not reach the monitor", err=True)
    _print(f"cb: monitoring target {target.name} — press Ctrl-] to quit")
    if elf is not None:
        _print(f"cb: monitor uses {elf.path} (sha256 {elf.sha256})")
    else:
        _print(f"cb: warning: no ELF in {build_dir} — build the target first (./cb build {target.name})", err=True)
    note = binaries_mod.monitor_mismatch_note(ctx, target=target.name, elf=elf)
    if note:
        _print(note)
    runner = Runner(ctx)
    streamed = runner.monitor_live(
        [
            "-C", str(target.path),
            "-B", str(build_dir),
            f"-DSDKCONFIG={build_dir / 'sdkconfig'}",
            "-p", port,
            "monitor",
        ],
        cwd=ctx.root,
        timeout=float(os.environ.get("CB_MONITOR_TIMEOUT", "86400")),
    )
    if streamed.timed_out:
        _print(f"cb: monitor stopped after {os.environ.get('CB_MONITOR_TIMEOUT', '86400')}s timeout")
        return CODE_OK
    return CODE_OK if streamed.code in (0, None) else CODE_FAIL


# ------------------------------------------------------------------------- selftest
# Frozen selftest serial grammar (ITEM6-BRIEF ST2 / ITEM6-RULINGS gold table,
# tightened by FIX-BRIEF-item6 round 2):
#   SELFTEST BEGIN version=1 cb_bsp=<ver>               (exactly one, first)
#   ITEM:<token> <PASS|FAIL|SKIP> [key=value ...]        (x7, frozen order; PASS lines
#                                                          carry the required evidence)
#   ITEM:button SKIP reason=needs-human poller=running   (the exact frozen line)
#   ITEM:sd SKIP reason=<no-card|absent>                 (sd is the only other SKIP;
#                                                          absent hardware elsewhere is a FAIL)
#   RESULT:<PASS|FAIL> total=7 pass=<p> fail=<f> skip=<s> (exactly one, last contract line)
# pass+fail+skip == total == 7; RESULT:PASS iff fail==0, RESULT:FAIL iff fail>=1.
# Any line containing ITEM:/RESULT: — or a second BEGIN anywhere — that fails
# the grammar is a hard FAIL of the run; no "best effort" accept. Monitor
# lines may carry ANSI escapes and the `I (123) tag: ` log prefix; both are
# stripped before matching, and bare (printf-style) lines are accepted too.
_SELFTEST_ITEMS = ("lifecycle", "lcd", "rgb", "button", "battery", "imu", "sd")
_SELFTEST_VERSION = "1"
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
_LOG_PREFIX_RE = re.compile(r"^[IWEVD] \([0-9]+\) [^:]{1,24}: ")
_BEGIN_RE = re.compile(r"^SELFTEST BEGIN version=(?P<v>[0-9]+) cb_bsp=(?P<ver>\S+)$")
_ITEM_RE = re.compile(r"^ITEM:(?P<tok>[a-z]+) (?P<st>PASS|FAIL|SKIP)(?P<rest>.*)$")
_RESULT_RE = re.compile(
    r"^RESULT:(?P<v>PASS|FAIL) total=(?P<t>[0-9]+) pass=(?P<p>[0-9]+) "
    r"fail=(?P<f>[0-9]+) skip=(?P<s>[0-9]+)$"
)
_KV_RE = re.compile(r"^[a-z0-9_]+=[^\s=]+$")

# FIX-BRIEF-item6 (rounds 2+3): the exact evidence a PASS line must carry per
# item. Each entry is (spec, expected-text): spec None = any value; a tuple =
# the allowed exact strings; a callable = numeric sanity (round 3).

def _int_in(lo: int, hi: int):
    def _check(value: str) -> bool:
        try:
            v = int(value)
        except ValueError:
            return False
        return lo <= v <= hi

    return _check


def _int_at_least(n: int):
    def _check(value: str) -> bool:
        try:
            return int(value) >= n
        except ValueError:
            return False

    return _check


def _finite_float_in(lo: float, hi: float):
    def _check(value: str) -> bool:
        try:
            v = float(value)
        except ValueError:
            return False
        return math.isfinite(v) and lo <= v <= hi

    return _check


_PASS_EVIDENCE = {
    "lifecycle": {"init": (("ESP_OK",), "ESP_OK")},
    "lcd": {
        "lvgl": (None, "a version"),
        "frames": (_int_at_least(1), "an integer >= 1"),
    },
    "rgb": {"cap_percent": (("20",), "20")},
    "battery": {
        "battery_mv": (_int_in(2500, 4500), "an integer in [2500, 4500]"),
        "plausible": (("1",), "1"),
        "calibrated": (("1",), "1"),
    },
    "imu": {
        "addr": (("0x6A", "0x6B"), "0x6A|0x6B"),
        "who": (("0x05",), "0x05"),
        "mag_g": (_finite_float_in(0.5, 1.5), "a finite float in [0.5, 1.5]"),
    },
    "sd": {"mounted": (("1",), "1"), "width": (("1", "4"), "1|4")},
    "button": {},
}
_BUTTON_LINE_FIELDS = ["poller=running"]   # after "reason=needs-human": the exact frozen line
_SELFTEST_DRAIN_S = 2.0   # FIX-BRIEF round 2: post-RESULT trailing-garbage window


def _selftest_payload(line: str) -> str:
    """Strip ANSI escapes, one optional IDF log prefix, and outer whitespace."""
    text = _ANSI_RE.sub("", line).strip()
    text = _LOG_PREFIX_RE.sub("", text, count=1)  # ^-anchored: only a leading prefix
    return text.strip()


def _selftest_target_name(available) -> str | None:
    """Pick the selftest firmware to flash (ITEM6-RULINGS: 06 first).

    07/08_selftest stay as documented fallbacks so a future rename surfaces as
    a build error in that project, not as a silent `./cb selftest` breakage.
    Iterating this preference tuple (never the caller's set) keeps the pick
    deterministic when several candidates exist.
    """
    for name in ("06_dashboard", "07_selftest", "08_selftest"):
        if name in available:
            return name
    return None


def _parse_selftest(lines: list[str]) -> tuple[bool, str, int]:
    """Validate the frozen selftest grammar; returns (ok, detail, skipped).

    ok is True only for a grammar-valid RESULT:PASS. detail names the violated
    rule on any rejection (ITEM6-RULINGS: no best-effort accept) and lists the
    failing item tokens on a grammar-valid RESULT:FAIL. skipped counts the SKIP
    items whenever the grammar validated (0 otherwise).
    """
    payload = [_selftest_payload(ln) for ln in lines]

    def contract(line: str) -> bool:
        # Round 3: ANY line containing SELFTEST is contract content too — it
        # must be a well-formed BEGIN or the run FAILs.
        return (
            "ITEM:" in line
            or "RESULT:" in line
            or "SELFTEST" in line
        )

    begin_idx = next((i for i, ln in enumerate(payload) if ln.startswith("SELFTEST BEGIN")), None)
    if begin_idx is None:
        seen = sum(1 for ln in payload if contract(ln))
        return False, f"no SELFTEST BEGIN line within timeout (saw {seen} contract line(s))", 0
    for ln in payload[:begin_idx]:
        if contract(ln):
            return False, f"contract line before SELFTEST BEGIN: '{ln}'", 0
    m = _BEGIN_RE.match(payload[begin_idx])
    if m is None:
        return False, f"malformed SELFTEST BEGIN line: '{payload[begin_idx]}'", 0
    if m.group("v") != _SELFTEST_VERSION:
        return False, f"unsupported selftest version={m.group('v')} (expected version=1)", 0

    items = 0
    counts = {"PASS": 0, "FAIL": 0, "SKIP": 0}
    failing: list[str] = []
    result_line = ""
    for ln in payload[begin_idx + 1 :]:
        if result_line:
            # RESULT must be the LAST contract line; live/alive lines after it are fine.
            if ln.startswith("RESULT:"):
                return False, f"second RESULT line after the terminal RESULT: '{ln}'", 0
            if contract(ln):
                return False, f"contract line after RESULT: '{ln}'", 0
            continue
        if "RESULT:" in ln:
            # Ruled strictness: any line CONTAINING the markers must be grammatical
            # (a stray marker in noise is a hard FAIL, not something to skip).
            if not ln.startswith("RESULT:"):
                return False, f"malformed RESULT line: '{ln}'", 0
            result_line = ln
            continue
        if "ITEM:" not in ln:
            if "SELFTEST" in ln:
                if ln.startswith("SELFTEST BEGIN"):
                    return False, "second SELFTEST BEGIN line", 0
                return False, f"malformed SELFTEST line: '{ln}'", 0
            continue  # boot/driver noise between BEGIN and the items
        # An ITEM line: grammar-check it and place it in the frozen order.
        im = _ITEM_RE.match(ln)
        if im is None:
            return False, f"malformed ITEM line: '{ln}'", 0
        tok, status, rest = im.group("tok"), im.group("st"), im.group("rest").strip()
        fields = rest.split() if rest else []
        if tok not in _SELFTEST_ITEMS:
            return False, f"unknown item token '{tok}'", 0
        if items >= len(_SELFTEST_ITEMS) or tok != _SELFTEST_ITEMS[items]:
            if items >= len(_SELFTEST_ITEMS):
                return False, f"expected RESULT after 7 items, saw '{ln}'", 0
            return False, (
                f"item out of order: expected '{_SELFTEST_ITEMS[items]}', "
                f"saw '{tok}'"
            ), 0
        if tok == "button" and status != "SKIP":
            return False, f"button must be SKIP reason=needs-human, saw status {status}", 0
        # Fields: k=v pairs (duplicate keys are malformed); SKIP leads with reason=.
        kv: dict[str, str] = {}
        for field in fields:
            if not _KV_RE.match(field):
                return False, f"malformed field '{field}' in: '{ln}'", 0
            key, _, val = field.partition("=")
            if key in kv:
                return False, f"duplicate key '{key}' in: '{ln}'", 0
            kv[key] = val
        if status == "SKIP":
            if not fields or not fields[0].startswith("reason="):
                return False, f"SKIP item without reason= as the first field: '{ln}'", 0
            reason = kv["reason"]
            if tok == "button":
                if reason != "needs-human":
                    return False, "button SKIP reason must be needs-human", 0
                if fields[1:] != _BUTTON_LINE_FIELDS:
                    return False, (
                        "button line must be exactly 'SKIP reason=needs-human "
                        "poller=running'"
                    ), 0
            elif tok == "sd":
                if reason not in ("no-card", "absent"):
                    return False, f"illegal SKIP reason '{reason}' for 'sd'", 0
            else:
                # FIX-BRIEF round 2: absent hardware on lifecycle/lcd/rgb/battery/imu
                # is a FAIL, never a SKIP.
                return False, f"SKIP is not legitimate for '{tok}' (absence is a FAIL)", 0
        else:
            if not fields:
                return False, f"{status} item without key=value evidence: '{ln}'", 0
            if status == "PASS":
                required = _PASS_EVIDENCE.get(tok, {})
                for key, (spec, want) in required.items():
                    if key not in kv:
                        return False, f"{tok} PASS missing required evidence key '{key}'", 0
                    if spec is not None and not (kv[key] in spec if isinstance(spec, tuple) else spec(kv[key])):
                        return False, (
                            f"{tok} PASS has {key}={kv[key]}, expected {want}"
                        ), 0
        items += 1
        counts[status] += 1
        if status == "FAIL":
            failing.append(tok)

    if not result_line:
        return False, f"no RESULT: line within timeout (saw {items} item(s))", 0
    if items != len(_SELFTEST_ITEMS):
        return False, f"expected 7 ITEM lines, saw {items}", 0
    rm = _RESULT_RE.match(result_line)
    if rm is None:
        return False, f"malformed RESULT line: '{result_line}'", 0
    total, p, f, s = (int(rm.group(k)) for k in ("t", "p", "f", "s"))
    verdict = rm.group("v")
    if total != 7:
        return False, f"total={total}, expected 7", 0
    if p + f + s != 7:
        return False, f"arithmetic mismatch: pass+fail+skip={p + f + s}, expected 7", 0
    if (p, f, s) != (counts["PASS"], counts["FAIL"], counts["SKIP"]):
        return (
            False,
            f"count mismatch: items say PASS={counts['PASS']} FAIL={counts['FAIL']} "
            f"SKIP={counts['SKIP']}, RESULT says pass={p} fail={f} skip={s}",
            0,
        )
    if verdict == "PASS" and f != 0:
        return False, f"RESULT:PASS with fail={f} > 0", 0
    if verdict == "FAIL" and f == 0:
        return False, "RESULT:FAIL with fail=0 (verdict must follow the items)", 0
    if verdict == "PASS":
        return True, result_line, s
    return False, f"{f} failing item(s): {', '.join(failing)}; {result_line}", s


def cmd_selftest(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    port = ports_mod.guard_port(args.port)  # guard BEFORE building/flashing
    available = {t.name for t in targets_mod.discover(ctx)}
    name = _selftest_target_name(available)
    if name is None:
        raise EnvError(
            "no selftest firmware in this tree yet (item 6 lands examples/06_dashboard); "
            "refusing to flash the plain app and wait for a RESULT: line"
        )
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
    # FIX-BRIEF-item6 (round 2): do not stop AT the first RESULT — keep listening
    # for a ~2 s drain window so trailing contract garbage (a second RESULT, a
    # stray ITEM:, a second BEGIN) is actually captured and rejected by the
    # parser. Line-level `stop` fires immediately on any contract marker AFTER
    # the terminal RESULT; `stop_check` ends the stream once the drain expires
    # (evaluated ~4 Hz even with no traffic, so a silent firmware cannot hold
    # the run open past the drain).
    result_at: list[float | None] = [None]

    def _saw_result(line: str) -> bool:
        if result_at[0] is None:
            if "RESULT:" in line:
                result_at[0] = time.monotonic()
            return False
        payload = _selftest_payload(line)
        return (
            "ITEM:" in payload
            or "RESULT:" in payload
            or "SELFTEST" in payload
        )

    def _drain_expired() -> bool:
        return result_at[0] is not None and (
            time.monotonic() - result_at[0] >= _SELFTEST_DRAIN_S
        )

    streamed = runner.stream(
        runner.idf_argv(
            [
            "-C", str(target.path),
            "-B", str(outcome.build_dir),
            f"-DSDKCONFIG={outcome.build_dir / 'sdkconfig'}",
            "-p", port,
            "monitor",
            ]
        ),
        cwd=ctx.root,
        timeout=timeout,
        echo=True,
        stop=_saw_result,
        stop_check=_drain_expired,
    )
    ok, detail, skipped = _parse_selftest(streamed.lines)
    if streamed.timed_out and not any("RESULT:" in line for line in streamed.lines):
        detail = f"timed out after {timeout:.0f}s; {detail}"
    _print(("[PASS] " if ok else "[FAIL] ") + "selftest " + detail)
    if skipped > 0:
        # ITEM6-RULINGS / R19: SKIP is never verification — say it loudly.
        _print(f"cb: {skipped} skipped (unverified)")
    return CODE_OK if ok else CODE_FAIL


# -------------------------------------------------------------------------- arduino
def cmd_arduino(args: argparse.Namespace) -> int:
    ctx = load_ctx()
    sketches = [t for t in targets_mod.discover(ctx) if t.kind == "arduino"]
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
    if not sketches and not args.upload:
        _print("no Arduino sketches found under arduino/ (expected arduino/sketches/<name>/<name>.ino)")
        return CODE_OK
    if args.sketches:
        wanted = set(args.sketches)
        unknown = wanted - {t.name for t in sketches}
        if unknown:
            raise UsageError(f"unknown sketch(es): {', '.join(sorted(unknown))}")
        sketches = [t for t in sketches if t.name in wanted]
    port: str | None = None
    if args.upload:
        # AR13: the port guard runs BEFORE any staging/build (exit 2 + BOOT/RESET hint).
        port = ports_mod.guard_port(args.port)
        if len(sketches) != 1:
            names = ", ".join(t.name for t in sketches) or "none discovered"
            raise UsageError(
                f"--upload needs exactly one sketch, got {len(sketches)} ({names}); "
                "pass a single sketch name, e.g. './cb arduino --upload rgb_blink'"
            )
    runner = Runner(ctx)
    failed: list[str] = []
    for sketch in sketches:
        outcome = builder.build_arduino(ctx, runner, sketch)
        _print(f"[{'PASS' if outcome.ok else 'FAIL'}] arduino:{sketch.name} {outcome.detail}")
        if not outcome.ok:
            failed.append(sketch.name)
            continue
        if args.upload:
            assert port is not None
            result = builder.upload_arduino(ctx, runner, sketch, port)
            _print(result.out)
            _print(result.err, err=True)
            if not result.ok:
                _print(f"[FAIL] upload arduino:{sketch.name} to {port}", err=True)
                failed.append(sketch.name)
    return CODE_FAIL if failed else CODE_OK


def cmd_arduino_install(args: argparse.Namespace) -> int:
    # AR9: install the SAME staged tree the gate compiles against (mirror-verified),
    # never a hand-maintained copy.
    ctx = load_ctx()
    staged, err = arduino_mod.stage_library(ctx)
    if err is not None or staged is None:
        raise EnvError(err or "staging failed")
    sketchbook = Path(os.environ.get("ARDUINO_SKETCHBOOK", str(Path.home() / "Arduino")))
    target_dir = sketchbook / "libraries"
    target_dir.mkdir(parents=True, exist_ok=True)
    dest = target_dir / staged.name
    if dest.exists():
        shutil.rmtree(dest)
    shutil.copytree(staged, dest)
    _print(f"installed {staged.name} (staged, {len(arduino_mod.REQUIRED_MIRROR)} mirror files) -> {dest}")
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
    p.add_argument("--target", help="IDF target whose build dir/ELF monitor uses (default: app)")
    p.set_defaults(func=cmd_monitor)

    p = sub.add_parser("selftest", help="flash the selftest and check the serial contract (guarded)")
    p.add_argument("--port")
    p.set_defaults(func=cmd_selftest)

    p = sub.add_parser("arduino", help="compile Arduino sketches (or upload one)")
    p.add_argument("sketches", nargs="*")
    p.add_argument("--install-libs", action="store_true")
    p.add_argument("--upload", action="store_true", help="upload one sketch (guarded port, exit 2 without a board)")
    p.add_argument("--port", help="serial port to upload to (default: the only /dev/ttyACM*|ttyUSB*)")
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
