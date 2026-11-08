"""verify(): step ids/order, statuses, report artifacts, exit-code mapping, fast/offline modes."""

from __future__ import annotations

import io
import json
import os
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import support
from support import FakeRunner, TempRepoTest, ctx_for, make_repo

from cbchecks import report, verify
from cbchecks.context import Ctx

HINT = "hold BOOT, tap RESET, release BOOT (download mode)"
SDKCONFIG = """CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
CONFIG_PARTITION_TABLE_CUSTOM=y
"""
PARTITION_ECHO = """Partition table binary generated. Contents:
# ESP-IDF Partition Table
factory, app, factory, 0x10000, 3M
storage, data, fat, 0x310000, 0x800000
"""


class BuildSimRunner(FakeRunner):
    """Simulates a successful IDF build by materialising the expected artifacts."""

    def __init__(self, ctx: Ctx, *, build_ok: bool = True):
        super().__init__(ctx)
        self.build_ok = build_ok
        self.reply("cb.py flash", 2, "", HINT)
        self.reply("cb.py monitor", 2, "", HINT)
        self.reply("make test", 0, "ok - all\n1..1\n")
        self.reply("-m unittest discover", 0, "", "Ran 52 tests in 0.1s\n\nOK\n")

    def run_idf(self, args, *, cwd, timeout=3600.0):  # type: ignore[override]
        result = super().run_idf(args, cwd=cwd)
        joined = " ".join(args)
        if joined.startswith("-C"):
            build_dir = Path(_flag(args, "-B"))
            if " build" in f" {joined}" and self.build_ok:
                build_dir.mkdir(parents=True, exist_ok=True)
                (build_dir / "sdkconfig").write_text(SDKCONFIG, encoding="utf-8")
                part = build_dir / "partition_table"
                part.mkdir(exist_ok=True)
                (part / "partition-table.bin").write_bytes(b"\xaa" * 3072)
                (build_dir / "child_beeper.bin").write_bytes(b"\x00" * 4096)
                from cbchecks.proc import Result

                return Result(list(args), 0, PARTITION_ECHO, "", 1)
        return result


def _flag(args: list[str], name: str) -> str:
    return args[list(args).index(name) + 1]


def _run_verify(root: Path, *, fast: bool = False, offline: bool = False, runner_cls=BuildSimRunner,
                no_arduino: bool = False) -> tuple[int, list, Ctx]:
    ctx = ctx_for(root, **({"offline": offline, "no_arduino": no_arduino}))
    runner = runner_cls(ctx)
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code, results = verify.verify(ctx, runner, fast=fast)
    _run_verify.last_output = out.getvalue() + err.getvalue()  # type: ignore[attr-defined]
    return code, results, ctx


_run_verify.last_output = ""  # type: ignore[attr-defined]


class TestVerifyHappyPath(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root, examples=("01_lcd_hello",))
        os.environ["CB_FORCE_NO_PORTS"] = "1"

    def test_step_ids_and_order(self) -> None:
        code, results, _ = _run_verify(self.root)
        ids = [r.id for r in results]
        self.assertEqual(
            ids,
            [
                "doctor",
                "host-tests",
                "cli-tests",
                "pin-drift",
                "audit-static",
                "build:app",
                "build:01_lcd_hello",
                "sdkconfig-assert",
                "arduino:*",
                "guards-no-board",
                "report",
            ],
        )
        self.assertEqual(code, 0, [r.detail for r in results if r.status != "PASS"])
        self.assertEqual([r.status for r in results if r.id == "pin-drift"], ["SKIP"])

    def test_report_files_and_schema(self) -> None:
        code, _, ctx = _run_verify(self.root)
        self.assertEqual(code, 0)
        md, js = report.report_paths(ctx)
        self.assertTrue(md.is_file())
        payload = json.loads(js.read_text(encoding="utf-8"))
        self.assertEqual(payload["schema"], 1)
        self.assertIn("build-verified", payload["verification_level"])
        self.assertEqual(payload["build_root"], str(ctx.build_root))
        self.assertEqual(payload["totals"]["fail"], 0)
        ids = [step["id"] for step in payload["steps"]]
        self.assertIn("build:app", ids)
        self.assertIn("guards-no-board", ids)
        self.assertEqual(ids[-1], "report")
        self.assertIn("sizes", payload["steps"][ids.index("build:app")]["extra"])

    def test_fast_mode_skips_builds_but_keeps_ids(self) -> None:
        code, results, _ = _run_verify(self.root, fast=True)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:app"].status, "SKIP")
        self.assertEqual(by_id["build:01_lcd_hello"].status, "SKIP")
        self.assertEqual(by_id["sdkconfig-assert"].status, "SKIP")
        self.assertEqual(by_id["doctor"].status, "PASS")
        self.assertEqual(by_id["host-tests"].status, "PASS")
        self.assertEqual(by_id["cli-tests"].status, "PASS")
        self.assertEqual(code, 0)

    def test_sdkconfig_assert_detects_missing_keys(self) -> None:
        ctx = ctx_for(self.root)
        runner = BuildSimRunner(ctx)
        bdir = ctx.build_root / "app"
        bdir.mkdir(parents=True)
        (bdir / "sdkconfig").write_text("CONFIG_SPIRAM=y\n", encoding="utf-8")
        (bdir / "partition_table").mkdir()
        ok, detail, _ = verify.builder.assert_sdkconfig(ctx, runner, _app_target(ctx), bdir)
        self.assertFalse(ok)
        self.assertIn("missing keys", detail)

    def test_sdkconfig_assert_detects_wrong_partition_csv(self) -> None:
        ctx = ctx_for(self.root)
        runner = BuildSimRunner(ctx)
        bdir = ctx.build_root / "app"
        bdir.mkdir(parents=True)
        (bdir / "sdkconfig").write_text(SDKCONFIG, encoding="utf-8")
        (bdir / "partition_table").mkdir()
        (bdir / "partition_table/partition-table.bin").write_bytes(b"\x00")
        ctx.log_path("build:app").parent.mkdir(parents=True, exist_ok=True)
        ctx.log_path("build:app").write_text("factory, app, factory, 0x10000, 3M\n", encoding="utf-8")
        ok, detail, _ = verify.builder.assert_sdkconfig(ctx, runner, _app_target(ctx), bdir)
        self.assertFalse(ok)
        self.assertIn("partition table", detail)


def _app_target(ctx: Ctx):
    from cbchecks import targets

    return targets.app_target(ctx)


class TestVerifyFailures(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        os.environ["CB_FORCE_NO_PORTS"] = "1"

    def test_missing_root_app_fails_build_app(self) -> None:
        make_repo(self.root, app=False, examples=())
        code, results, _ = _run_verify(self.root)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:app"].status, "FAIL")
        self.assertIn("glm53", by_id["build:app"].detail)
        self.assertEqual(code, 1)

    def test_missing_required_example_fails(self) -> None:
        make_repo(self.root, examples=())
        code, results, _ = _run_verify(self.root)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:01_lcd_hello"].status, "FAIL")
        self.assertEqual(code, 1)

    def test_missing_host_tests_fails_per_ruling_r6(self) -> None:
        make_repo(self.root, host_tests=False)
        code, results, _ = _run_verify(self.root)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["host-tests"].status, "FAIL")
        self.assertIn("kimi-k31-pro", by_id["host-tests"].detail)
        self.assertEqual(code, 1)

    def test_build_failure_fails_and_still_writes_report(self) -> None:
        make_repo(self.root)
        code, results, ctx = _run_verify(self.root, runner_cls=lambda c: BuildSimRunner(c, build_ok=False))
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:app"].status, "FAIL")
        self.assertEqual(code, 1)
        self.assertTrue(report.report_paths(ctx)[1].is_file())
        self.assertEqual(by_id["report"].status, "PASS")

    def test_doctor_failure_maps_to_exit_three(self) -> None:
        make_repo(self.root)

        class BadIdfRunner(BuildSimRunner):
            def idf_version(self) -> str:
                return "ESP-IDF v5.0"

        code, results, _ = _run_verify(self.root, runner_cls=BadIdfRunner)
        self.assertEqual({r.id: r.status for r in results}["doctor"], "FAIL")
        self.assertEqual(code, 3)

    def test_guard_step_fails_when_flash_does_not_exit_two(self) -> None:
        make_repo(self.root)

        class NoGuardRunner(BuildSimRunner):
            def __init__(self, ctx):
                super().__init__(ctx)
                self.reply("cb.py flash", 0, "", "")

        code, results, _ = _run_verify(self.root, runner_cls=NoGuardRunner)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["guards-no-board"].status, "FAIL")
        self.assertIn("expected 2", by_id["guards-no-board"].detail)
        self.assertEqual(code, 1)


class TestVerifyOffline(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        os.environ["CB_FORCE_NO_PORTS"] = "1"

    def test_offline_without_unshare_skips_builds_with_explicit_reason(self) -> None:
        class NoUnshareRunner(BuildSimRunner):
            def offline_available(self) -> bool:
                return False

        code, results, _ = _run_verify(self.root, offline=True, runner_cls=NoUnshareRunner)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:app"].status, "SKIP")
        self.assertIn("NOT proven offline", by_id["build:app"].detail)
        self.assertEqual(code, 1, "skipped builds without --fast must fail the gate")
        self.assertIn("gate NOT fully proven", _run_verify.last_output)

    def test_offline_with_unshare_still_builds(self) -> None:
        class UnshareRunner(BuildSimRunner):
            def offline_available(self) -> bool:
                return True

        code, results, ctx = _run_verify(self.root, offline=True, runner_cls=UnshareRunner)
        self.assertEqual({r.id: r.status for r in results}["build:app"], "PASS")
        self.assertEqual(code, 0)
        payload = json.loads(report.report_paths(ctx)[1].read_text(encoding="utf-8"))
        self.assertTrue(payload["mode"]["offline_namespace"])


def _write_partition_bin(path: Path, rows) -> None:
    """Write a real ESP-IDF partition-table.bin (32-byte LE entries, magic 0xAA50)."""
    import struct

    entry = struct.Struct("<HBBII16sI")
    blob = b""
    for name, ptype, subtype, offset, size in rows:
        blob += entry.pack(0x50AA, ptype, subtype, offset, size, name.encode().ljust(16, b"\0"), 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(blob)


GOOD_ROWS = [
    ("nvs", 1, 0x02, 0x9000, 0x6000),
    ("phy_init", 1, 0x01, 0xF000, 0x1000),
    ("factory", 0, 0x00, 0x10000, 3 * 1024 * 1024),
    ("storage", 1, 0x81, 0x310000, 8 * 1024 * 1024),
]
BAD_ROWS = [
    ("factory", 0, 0x00, 0x10000, 1 * 1024 * 1024),
    ("storage", 1, 0x81, 0x310000, 1 * 1024 * 1024),
]


class TestPartitionTableDecoding(TempRepoTest):
    """P0-1: the assertion must read the built table, not the (incremental) build log."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)
        self.runner = BuildSimRunner(self.ctx)
        self.bdir = self.ctx.build_root / "app"
        self.bdir.mkdir(parents=True)
        (self.bdir / "sdkconfig").write_text(SDKCONFIG, encoding="utf-8")

    def test_passes_without_any_build_log(self) -> None:
        _write_partition_bin(self.bdir / "partition_table/partition-table.bin", GOOD_ROWS)
        self.assertFalse(self.ctx.log_path("build:app").exists(), "incremental-build scenario")
        ok, detail, extra = verify.builder.assert_sdkconfig(
            self.ctx, self.runner, _app_target(self.ctx), self.bdir
        )
        self.assertTrue(ok, detail)
        self.assertEqual(extra["partition_source"], "binary decode")
        self.assertTrue(any(row["name"] == "storage" for row in extra["partition_rows"]))

    def test_prefers_gen_esp32part_output(self) -> None:
        _write_partition_bin(self.bdir / "partition_table/partition-table.bin", BAD_ROWS)
        self.runner.reply(
            "gen_esp32part.py",
            0,
            "# ESP-IDF Partition Table\n"
            "nvs,data,nvs,0x9000,24K,\n"
            "phy_init,data,phy,0xf000,4K,\n"
            "factory,app,factory,0x10000,3M,\n"
            "storage,data,fat,0x310000,8M,\n",
            "",
        )
        ok, detail, extra = verify.builder.assert_sdkconfig(
            self.ctx, self.runner, _app_target(self.ctx), self.bdir
        )
        self.assertTrue(ok, detail)
        self.assertEqual(extra["partition_source"], "gen_esp32part.py")

    def test_fails_on_wrong_sizes_even_with_a_log(self) -> None:
        _write_partition_bin(self.bdir / "partition_table/partition-table.bin", BAD_ROWS)
        self.ctx.log_path("build:app").parent.mkdir(parents=True, exist_ok=True)
        self.ctx.log_path("build:app").write_text(
            "factory, app, factory, 0x10000, 1M\nstorage, data, fat, 0x310000, 1M\n",
            encoding="utf-8",
        )
        ok, detail, _ = verify.builder.assert_sdkconfig(
            self.ctx, self.runner, _app_target(self.ctx), self.bdir
        )
        self.assertFalse(ok)
        self.assertIn("partition table does not match", detail)

    def test_log_rows_still_work_as_or_fallback(self) -> None:
        bin_path = self.bdir / "partition_table/partition-table.bin"
        bin_path.parent.mkdir(parents=True, exist_ok=True)
        bin_path.write_bytes(b"\x00" * 128)  # undecodable on purpose
        self.ctx.log_path("build:app").parent.mkdir(parents=True, exist_ok=True)
        self.ctx.log_path("build:app").write_text(PARTITION_ECHO, encoding="utf-8")
        ok, detail, extra = verify.builder.assert_sdkconfig(
            self.ctx, self.runner, _app_target(self.ctx), self.bdir
        )
        self.assertTrue(ok, detail)
        self.assertIn("build log", extra["partition_source"])


class TestRequiredLayout(TempRepoTest):
    """P1-8: --fast must not hide a missing required project."""

    def setUp(self) -> None:
        super().setUp()
        os.environ["CB_FORCE_NO_PORTS"] = "1"

    def test_fast_mode_still_fails_without_the_root_app(self) -> None:
        make_repo(self.root, app=False, examples=())
        code, results, _ = _run_verify(self.root, fast=True)
        by_id = {r.id: r for r in results}
        self.assertEqual(by_id["build:app"].status, "FAIL")
        self.assertEqual(code, 1)

    def test_fast_mode_still_fails_without_the_required_example(self) -> None:
        make_repo(self.root, examples=())
        code, results, _ = _run_verify(self.root, fast=True)
        self.assertEqual({r.id: r for r in results}["build:01_lcd_hello"].status, "FAIL")
        self.assertEqual(code, 1)


class TestBuildAllScope(TempRepoTest):
    def test_build_all_ignores_arduino_sketches(self) -> None:
        import cb as cli

        make_repo(self.root, arduino=True)
        built: list[str] = []

        class RecordingRunner(BuildSimRunner):
            def run_idf(self, args, *, cwd, timeout=3600.0):  # type: ignore[override]
                built.append(" ".join(args))
                return super().run_idf(args, cwd=cwd, timeout=timeout)

        saved = cli.Runner
        cli.Runner = lambda ctx: RecordingRunner(ctx)  # type: ignore[assignment]
        try:
            with support.use_repo(self.root):
                out = io.StringIO()
                with redirect_stdout(out):
                    code = cli.main(["build", "--all"])
        finally:
            cli.Runner = saved  # type: ignore[assignment]
        self.assertEqual(code, 0, out.getvalue())
        self.assertTrue(any("examples/01_lcd_hello" in cmd for cmd in built), built)
        self.assertFalse(any("01_smoke" in cmd for cmd in built), "arduino must not be built here")


class TestDeps(TempRepoTest):
    def test_deps_reports_success_when_sdkconfig_is_generated(self) -> None:
        import cb as cli

        make_repo(self.root)

        class DepsRunner(BuildSimRunner):
            def run_idf(self, args, *, cwd, timeout=3600.0):  # type: ignore[override]
                build_dir = Path(_flag(list(args), "-B"))
                build_dir.mkdir(parents=True, exist_ok=True)
                (build_dir / "sdkconfig").write_text(SDKCONFIG, encoding="utf-8")
                from cbchecks.proc import Result

                return Result(["idf.py", *args], 0, "", "", 1)

        saved = cli.Runner
        cli.Runner = lambda ctx: DepsRunner(ctx)  # type: ignore[assignment]
        try:
            with support.use_repo(self.root):
                out = io.StringIO()
                with redirect_stdout(out):
                    code = cli.main(["deps"])
        finally:
            cli.Runner = saved  # type: ignore[assignment]
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("[PASS] deps app", out.getvalue())

    def test_deps_fails_without_sdkconfig(self) -> None:
        import cb as cli

        make_repo(self.root)
        saved = cli.Runner
        cli.Runner = lambda ctx: FakeRunner(ctx)  # type: ignore[assignment]
        try:
            with support.use_repo(self.root):
                out = io.StringIO()
                with redirect_stdout(out):
                    code = cli.main(["deps"])
        finally:
            cli.Runner = saved  # type: ignore[assignment]
        self.assertEqual(code, 1)


class TestSizesFallback(TempRepoTest):
    def test_bin_size_fallback_is_always_produced(self) -> None:
        from cbchecks import sizes
        from cbchecks import targets

        make_repo(self.root)
        ctx = ctx_for(self.root)
        runner = FakeRunner(ctx)
        runner.reply("size --format json", 1, "", "idf.py size failed")
        target = targets.app_target(ctx)
        bdir = ctx.build_root / "app"
        bdir.mkdir(parents=True)
        (bdir / "child_beeper.bin").write_bytes(b"\x00" * 1234)
        info = sizes.sizes_for(ctx, runner, target, bdir)
        self.assertTrue(info["fallback"])
        self.assertEqual(info["bin_bytes"], 1234)
        self.assertEqual(info["source"], "bin")


if __name__ == "__main__":
    unittest.main()
