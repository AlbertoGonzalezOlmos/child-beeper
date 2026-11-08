"""verify-report.md / verify-report.json writer.

Ruling R10: reports live at ${CB_BUILD_ROOT:-build}/verify-report.{md,json}.
"""

from __future__ import annotations

import json
import os
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Sequence

from .context import Ctx, arduino_cli
from .proc import Runner

SCHEMA = 1
SIZES_PLACEHOLDER = {"bin": None, "bin_bytes": None, "source": "not-built", "fallback": True}


def normalise_step(result) -> dict:
    """Build steps always expose `sizes` (dict, never null) and `artifacts` (list, never absent)."""
    extra = dict(result.extra or {})
    if result.id.startswith("build:") or result.id.startswith("arduino:"):
        sizes = extra.get("sizes")
        extra["sizes"] = dict(sizes) if isinstance(sizes, dict) else dict(SIZES_PLACEHOLDER)
        if extra["sizes"].get("bin_bytes") is None:
            extra["sizes"].setdefault("source", "not-built")
        artifacts = extra.get("artifacts")
        extra["artifacts"] = list(artifacts) if isinstance(artifacts, list) else []
    return {
        "id": result.id,
        "status": result.status,
        "detail": result.detail,
        "duration_ms": result.duration_ms,
        "extra": extra,
    }
VERIFICATION_LEVEL = "build-verified (compile/CLI only; hardware validation is tracked separately in docs/verification.md)"


def report_paths(ctx: Ctx) -> tuple[Path, Path]:
    base = ctx.build_root
    return base / "verify-report.md", base / "verify-report.json"


def _git(ctx: Ctx, runner: Runner | None) -> dict:
    runner = runner or Runner(ctx)
    branch = runner.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=ctx.root, timeout=30)
    sha = runner.run(["git", "rev-parse", "HEAD"], cwd=ctx.root, timeout=30)
    status = runner.run(["git", "status", "--porcelain"], cwd=ctx.root, timeout=60)
    return {
        "branch": branch.out.strip() if branch.ok else None,
        "sha": sha.out.strip() if sha.ok else None,
        "dirty": bool(status.out.strip()) if status.ok else None,
    }


def meta(ctx: Ctx, runner: Runner | None = None, *, mode: dict | None = None) -> dict:
    runner = runner or Runner(ctx)
    try:
        idf = runner.idf_version()
    except Exception:  # noqa: BLE001 - the report must never fail on a probe
        idf = None
    cli = arduino_cli()
    arduino: str | None = None
    if cli is not None:
        probe = runner.run([str(cli), "version"], timeout=60)
        first = probe.out.strip().splitlines()[0] if probe.ok and probe.out.strip() else ""
        arduino = first or str(cli)
    return {
        "schema": SCHEMA,
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "mode": mode or {},
        "repo": _git(ctx, runner),
        "toolchain": {
            "python": ".".join(str(v) for v in __import__("sys").version_info[:3]),
            "idf": idf,
            "arduino_cli": arduino,
        },
        "build_root": str(ctx.build_root),
        "verification_level": VERIFICATION_LEVEL,
    }


def _totals(results: Sequence) -> dict:
    totals = {"pass": 0, "fail": 0, "skip": 0, "duration_ms": 0}
    for result in results:
        key = result.status.lower()
        if key in totals:
            totals[key] += 1
        totals["duration_ms"] += result.duration_ms
    return totals


def render_markdown(meta_dict: dict, results: Sequence) -> str:
    totals = _totals(results)
    lines = [
        "# verify report",
        "",
        f"**{VERIFICATION_LEVEL}**.",
        "",
        f"- generated: {meta_dict.get('generated_at')}",
        f"- branch/sha: {meta_dict.get('repo', {}).get('branch')} / {meta_dict.get('repo', {}).get('sha')}",
        f"- build root: `{meta_dict.get('build_root')}`",
        f"- toolchain: {meta_dict.get('toolchain')}",
        f"- mode: {meta_dict.get('mode')}",
        "",
        f"Totals: {totals['pass']} pass, {totals['fail']} fail, {totals['skip']} skip "
        f"({totals['duration_ms'] / 1000.0:.1f}s)",
        "",
        "| step | status | detail | ms |",
        "|---|---|---|---|",
    ]
    for result in results:
        detail = result.detail.replace("|", "\\|")
        lines.append(f"| `{result.id}` | {result.status} | {detail} | {result.duration_ms} |")
    lines.append("")
    for result in results:
        if not result.extra:
            continue
        lines.append(f"### {result.id}")
        lines.append("```json")
        lines.append(json.dumps(result.extra, indent=2, sort_keys=True)[:4000])
        lines.append("```")
    lines.append("")
    return "\n".join(lines)


def write_report(ctx: Ctx, results: Sequence, meta_dict: dict) -> tuple[Path, Path]:
    md_path, json_path = report_paths(ctx)
    md_path.parent.mkdir(parents=True, exist_ok=True)
    payload = dict(meta_dict)
    payload["totals"] = _totals(results)
    payload["steps"] = [normalise_step(r) for r in results]
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    md_path.write_text(render_markdown(meta_dict, results), encoding="utf-8")
    return md_path, json_path
