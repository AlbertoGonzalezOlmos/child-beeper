"""Flash records + monitor target selection: the 'Checksum mismatch' warning must be explainable."""
from __future__ import annotations

import io
import json
import os
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import support
from support import TempRepoTest, ctx_for, make_repo, use_repo

from cbchecks import binaries
from cbchecks.proc import Result


def _write_elf(build_dir: Path, name: str, payload: bytes) -> Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "project_description.json").write_text(
        json.dumps({"app_elf": name}), encoding="utf-8"
    )
    elf = build_dir / name
    elf.write_bytes(payload)
    return elf


class TestBinaries(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_app_elf_comes_from_project_description(self) -> None:
        elf = _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-a")
        self.assertEqual(binaries.app_elf(self.ctx.build_root / "app"), elf)

    def test_app_elf_falls_back_to_glob(self) -> None:
        build_dir = self.ctx.build_root / "01_lcd_hello"
        build_dir.mkdir(parents=True)
        (build_dir / "01_lcd_hello.elf").write_bytes(b"elf-b")
        self.assertEqual(binaries.app_elf(build_dir).name, "01_lcd_hello.elf")

    def test_flash_record_round_trip(self) -> None:
        elf = _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-a")
        info = binaries.elf_info(self.ctx.build_root / "app")
        record = binaries.write_flash_record(
            self.ctx, build_dir=elf.parent, elf=info, port="/dev/ttyACM0", target="app"
        )
        self.assertTrue(record.is_file())
        data = json.loads(record.read_text())
        self.assertEqual(data["target"], "app")
        self.assertEqual(data["sha256"], info.sha256)
        self.assertEqual(data["port"], "/dev/ttyACM0")
        self.assertEqual(binaries.read_flash_record(self.ctx)["elf"], str(elf))

    def test_no_note_without_a_flash_record(self) -> None:
        _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-a")
        info = binaries.elf_info(self.ctx.build_root / "app")
        self.assertIsNone(binaries.monitor_mismatch_note(self.ctx, target="app", elf=info))

    def test_note_explains_a_rebuilt_elf(self) -> None:
        elf = _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-old")
        binaries.write_flash_record(
            self.ctx,
            build_dir=elf.parent,
            elf=binaries.elf_info(elf.parent),
            port="/dev/ttyACM0",
            target="app",
        )
        elf.write_bytes(b"elf-new")  # rebuilt after the flash
        note = binaries.monitor_mismatch_note(
            self.ctx, target="app", elf=binaries.elf_info(elf.parent)
        )
        self.assertIsNotNone(note)
        self.assertIn("cosmetic", note)
        self.assertIn("./cb flash --target app", note)

    def test_note_warns_when_monitoring_a_different_target(self) -> None:
        elf = _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-a")
        binaries.write_flash_record(
            self.ctx, build_dir=elf.parent, elf=binaries.elf_info(elf.parent),
            port="/dev/ttyACM0", target="app",
        )
        other = _write_elf(self.ctx.build_root / "01_lcd_hello", "01_lcd_hello.elf", b"elf-c")
        note = binaries.monitor_mismatch_note(
            self.ctx, target="01_lcd_hello", elf=binaries.elf_info(other.parent)
        )
        self.assertIn("may not match the running image", note)

    def test_same_elf_after_flash_produces_no_note(self) -> None:
        elf = _write_elf(self.ctx.build_root / "app", "child_beeper.elf", b"elf-a")
        binaries.write_flash_record(
            self.ctx, build_dir=elf.parent, elf=binaries.elf_info(elf.parent),
            port="/dev/ttyACM0", target="app",
        )
        self.assertIsNone(
            binaries.monitor_mismatch_note(self.ctx, target="app", elf=binaries.elf_info(elf.parent))
        )


class TestMonitorTargetSelection(TempRepoTest):
    """`./cb monitor --target X` must use X's build dir (the ELF idf_monitor decodes against)."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        os.environ["CB_BUILD_ROOT"] = "build/agent-x"  # what load_ctx() resolves inside cb.py
        for name in ("app", "01_lcd_hello"):
            _write_elf(self.ctx_build() / name, f"{name}.elf", name.encode())

    def ctx_build(self) -> Path:
        return self.root / "build/agent-x"

    def _run_monitor(self, argv: list[str]) -> tuple[int, str, list[str]]:
        import cb as cli

        calls: list[list[str]] = []

        class MonitorRunner(cli.Runner):  # type: ignore[misc]
            def __init__(self, ctx):
                super().__init__(ctx)

            def run(self, argv, **kwargs):  # type: ignore[override]
                calls.append([str(a) for a in argv])
                return Result([str(a) for a in argv], 0, "", "", 1)

            def monitor_live(self, args, *, cwd, timeout):  # type: ignore[override]
                calls.append([str(a) for a in args])
                return type("S", (), {"timed_out": False, "code": 0, "lines": []})()

        saved = cli.Runner
        cli.Runner = MonitorRunner  # type: ignore[assignment]
        out, err = io.StringIO(), io.StringIO()
        try:
            with use_repo(self.root), redirect_stdout(out), redirect_stderr(err):
                code = cli.main(argv)
        finally:
            cli.Runner = saved  # type: ignore[assignment]
        return code, out.getvalue(), calls

    def test_default_monitors_the_root_app(self) -> None:
        code, out, calls = self._run_monitor(["monitor", "--port", "/dev/ttyACM0"])
        self.assertEqual(code, 0, out)
        self.assertIn("monitoring target app", out)
        self.assertTrue(any("-B" in c and c[c.index("-B") + 1].endswith("build/agent-x/app") for c in calls), calls)

    def test_target_selects_that_build_dir(self) -> None:
        code, out, calls = self._run_monitor(
            ["monitor", "--port", "/dev/ttyACM0", "--target", "01_lcd_hello"]
        )
        self.assertEqual(code, 0, out)
        self.assertIn("monitoring target 01_lcd_hello", out)
        self.assertIn("01_lcd_hello.elf", out)
        monitor_call = calls[-1]
        self.assertEqual(monitor_call[monitor_call.index("-B") + 1],
                         str(self.ctx_build() / "01_lcd_hello"))

    def test_unknown_monitor_target_is_usage_error(self) -> None:
        code, _, _ = self._run_monitor(["monitor", "--target", "nope"])
        self.assertEqual(code, 3)


if __name__ == "__main__":
    unittest.main()
