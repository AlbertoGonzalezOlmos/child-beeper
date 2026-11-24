"""Item 7 Arduino tooling: mirror manifest, staging, real pin-drift, upload guard.

AR8: cbchecks.arduino is the sole mirror.txt parser — fail-closed, frozen 12-entry set.
AR9: staging destination + locked-FQBN compile wiring.
AR10: pin-drift is a byte-for-byte staged-vs-canonical compare (mutated byte / dropped
      manifest line / post-stage canonical change / committed copy all FAIL).
AR13: `./cb arduino --upload` guards the port before building and needs exactly one sketch
      (zero or multiple → exit 3; round-2 B1).
AR14: doctor escalates arduino-cli + core to fatal once arduino/ exists.
Round 2: AR10's clean missing-canonical wording (B2) and the committed-fork ban — the library
      is umbrella + manifest only, any other committed code file FAILs staging and pin-drift (B3).
"""

from __future__ import annotations

import io
import os
import shutil
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import support
from support import ARDUINO_MIRROR, FakeRunner, TempRepoTest, ctx_for, make_repo, use_repo

from cbchecks import arduino, doctor, targets
from cbchecks.context import Ctx


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class TestManifest(TempRepoTest):
    """AR8: the manifest parser is fail-closed and the set is frozen at 12 entries."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)
        self.manifest = arduino.manifest_path(self.ctx)

    def _parse(self, text: str):
        self.manifest.write_text(text, encoding="utf-8")
        return arduino.parse_manifest(self.manifest)

    def test_happy_path_returns_the_frozen_set_in_order(self) -> None:
        entries, err = arduino.parse_manifest(self.manifest)
        self.assertIsNone(err)
        self.assertEqual(entries, ARDUINO_MIRROR)
        self.assertEqual(len(entries), 12)

    def test_missing_manifest_file_is_an_error(self) -> None:
        self.manifest.unlink()
        entries, err = arduino.parse_manifest(self.manifest)
        self.assertIsNone(entries)
        self.assertIn("missing", err)

    def test_absolute_path_is_rejected(self) -> None:
        entries, err = self._parse("/etc/passwd\n")
        self.assertIsNone(entries)
        self.assertIn("expected include/*.h or src/*.c", err)

    def test_parent_escape_is_rejected(self) -> None:
        entries, err = self._parse("../components/cb_bsp/include/cb_pins.h\n")
        self.assertIsNone(entries)
        self.assertIn("expected include/*.h or src/*.c", err)

    def test_outside_dirs_are_rejected(self) -> None:
        entries, err = self._parse("docs/cb_pins.h\n")
        self.assertIsNone(entries)
        self.assertIn("expected include/*.h or src/*.c", err)

    def test_duplicate_entry_is_rejected(self) -> None:
        entries, err = self._parse("\n".join(ARDUINO_MIRROR) + "\ninclude/cb_math.h\n")
        self.assertIsNone(entries)
        self.assertIn("duplicate entry", err)

    def test_dropped_entry_fails_set_mismatch(self) -> None:
        # The memory-mandated negative: deleting a manifest line must FAIL, not shrink.
        dropped = "\n".join(e for e in ARDUINO_MIRROR if e != "src/cb_imu_math.c") + "\n"
        entries, err = self._parse(dropped)
        self.assertIsNone(entries)
        self.assertIn("missing required entry src/cb_imu_math.c", err)

    def test_extra_entry_fails_set_mismatch(self) -> None:
        extra = "\n".join(ARDUINO_MIRROR) + "\ninclude/cb_extra.h\n"
        entries, err = self._parse(extra)
        self.assertIsNone(entries)
        self.assertIn("unexpected entry", err)

    def test_comments_and_blank_lines_are_allowed(self) -> None:
        text = "# a comment\n\n" + "\n".join(ARDUINO_MIRROR) + "\n# trailing\n"
        entries, err = self._parse(text)
        self.assertIsNone(err)
        self.assertEqual(entries, ARDUINO_MIRROR)


class TestStaging(TempRepoTest):
    """AR9: wipe-and-copy staging into ${CB_BUILD_ROOT:-build}/arduino/libraries/CBBoard."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_stages_the_12_mirror_files_plus_umbrella_into_src(self) -> None:
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(err, err)
        assert staged is not None
        src = staged / "src"
        staged_names = sorted(p.name for p in src.iterdir() if p.is_file())
        self.assertEqual(
            staged_names,
            sorted([Path(rel).name for rel in ARDUINO_MIRROR] + ["CBBoard.h"]),
        )
        for rel in ARDUINO_MIRROR:
            self.assertEqual(
                (src / Path(rel).name).read_bytes(),
                (self.ctx.root / "components" / "cb_bsp" / rel).read_bytes(),
                rel,
            )
        # committed library metadata rides along at the library root
        self.assertTrue((staged / "library.properties").is_file())
        self.assertTrue((staged / "mirror.txt").is_file())

    def test_staging_is_always_fresh(self) -> None:
        arduino.stage_library(self.ctx)
        staged_src = arduino.staged_library(self.ctx) / "src"
        stale = staged_src / "cb_pins.h"
        _write(stale, "/* stale bytes */\n")
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(err)
        self.assertEqual(
            stale.read_bytes(),
            (self.ctx.root / "components" / "cb_bsp" / "include" / "cb_pins.h").read_bytes(),
        )

    def test_manifest_naming_a_missing_canonical_file_fails(self) -> None:
        (self.ctx.root / "components" / "cb_bsp" / "src" / "cb_imu_math.c").unlink()
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(staged)
        self.assertIn("manifest names missing file: components/cb_bsp/src/cb_imu_math.c", err)

    def test_ino_inside_the_library_is_rejected(self) -> None:
        # AR15: sketch discovery would treat a library .ino as a sketch.
        _write(
            self.ctx.root / "arduino" / "libraries" / "CBBoard" / "oops.ino",
            "void setup(){}\n",
        )
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(staged)
        self.assertIn(".ino", err)


class TestPinDrift(TempRepoTest):
    """AR10: pin-drift is a real byte compare, not a presence check."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def _stage(self) -> Path:
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(err, err)
        assert staged is not None
        return staged / "src"

    def test_skips_only_while_arduino_tree_is_absent(self) -> None:
        shutil.rmtree(self.ctx.root / "arduino")
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "SKIP")
        self.assertIn("arduino", detail)

    def test_fresh_stage_passes_byte_equal(self) -> None:
        self._stage()
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "PASS")
        self.assertIn("12 mirror file(s) staged byte-equal", detail)

    def test_mutated_staged_byte_fails(self) -> None:
        # The memory-mandated negative.
        src = self._stage()
        data = bytearray((src / "cb_pins.h").read_bytes())
        data[-2] ^= 0x01  # flip one byte inside the file
        (src / "cb_pins.h").write_bytes(bytes(data))
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("differs from components/cb_bsp/include/cb_pins.h", detail)

    def test_dropped_manifest_line_fails_pin_drift(self) -> None:
        # The memory-mandated negative: the set-size != 12 case.
        self._stage()
        manifest = arduino.manifest_path(self.ctx)
        kept = [ln for ln in manifest.read_text().splitlines() if ln != "src/cb_rgb_math.c"]
        manifest.write_text("\n".join(kept) + "\n", encoding="utf-8")
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("missing required entry", detail)

    def test_canonical_changed_after_staging_is_reported_not_repaired(self) -> None:
        src = self._stage()
        _write(self.ctx.root / "components" / "cb_bsp" / "include" / "cb_math.h", "/* drifted */\n")
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("differs from components/cb_bsp/include/cb_math.h", detail)
        # read-only: the stale stage is left in place for the report to quote
        self.assertTrue((src / "cb_math.h").is_file())

    def test_missing_canonical_file_fails_with_ruled_wording(self) -> None:
        # Round-2 B2: a complete stage + a deleted canonical file is a clean FAIL with
        # AR10's wording, not a FileNotFoundError exception detail.
        self._stage()
        (self.ctx.root / "components" / "cb_bsp" / "src" / "cb_imu_math.c").unlink()
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("components/cb_bsp/src/cb_imu_math.c missing", detail)

    def test_committed_mirror_copy_under_arduino_fails(self) -> None:
        self._stage()
        _write(
            self.ctx.root / "arduino" / "libraries" / "CBBoard" / "extra" / "cb_rgb_math.c",
            "/* forked copy */\n",
        )
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("committed mirror copy", detail)
        # Round-2 B3: staging refuses it too, with the same (better) mirror message.
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(staged)
        self.assertIn("committed mirror copy", err)

    def test_committed_fork_code_file_fails_staging_and_pin_drift(self) -> None:
        # Round-2 B3: the library is umbrella + manifest only — helpers.h is a fork.
        fork = self.ctx.root / "arduino" / "libraries" / "CBBoard" / "src" / "helpers.h"
        _write(fork, "/* forked helper */\n")
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(staged)
        self.assertIn("arduino/libraries/CBBoard/src/helpers.h", err)
        self.assertIn(
            "unexpected committed code file in the library (AR1: umbrella + manifest only)", err
        )
        # pin-drift FAILs — both on a fresh tree (bootstrap refuses to stage) …
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("unexpected committed code file in the library", detail)
        # … and when a byte-equal stage predates the fork: the scan is unconditional,
        # so even a --fast gate (builds skipped, pin-drift still runs) catches it.
        fork.unlink()
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(err, err)
        assert staged is not None
        _write(fork, "/* forked helper */\n")
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("unexpected committed code file in the library", detail)

    def test_missing_library_dir_fails(self) -> None:
        shutil.rmtree(self.ctx.root / "arduino" / "libraries" / "CBBoard")
        status, detail = arduino.pin_drift(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("missing", detail)


class TestArduinoCommand(TempRepoTest):
    """`./cb arduino` compile/upload wiring (AR9/AR13)."""

    def _run_cli(self, argv: list[str], runner_cls=None) -> tuple[int, str, FakeRunner]:
        import cb as cli

        ctx = ctx_for(self.root)  # pins CB_BUILD_ROOT for both the CLI and the runner
        self.build_root = ctx.build_root
        runner = (runner_cls or FakeRunner)(ctx)
        saved = cli.Runner
        cli.Runner = lambda _ctx: runner  # type: ignore[assignment]
        try:
            out, err = io.StringIO(), io.StringIO()
            with use_repo(self.root), redirect_stdout(out), redirect_stderr(err):
                code = cli.main(argv)
        finally:
            cli.Runner = saved  # type: ignore[assignment]
        return code, out.getvalue() + err.getvalue(), runner

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        os.environ["CB_FORCE_NO_PORTS"] = "1"

    def test_compile_uses_locked_fqbn_and_staged_libraries(self) -> None:
        code, output, runner = self._run_cli(["arduino"])
        self.assertEqual(code, 0, output)
        commands = runner.commands()
        self.assertEqual(len(commands), 2, commands)
        for command in commands:
            self.assertIn("compile", command)
            self.assertIn(
                "--fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,CDCOnBoot=cdc,"
                "USBMode=hwcdc,PartitionScheme=default",
                command,
            )
            self.assertIn("--libraries", command)
            self.assertIn(str(self.build_root / "arduino" / "libraries"), command)
        self.assertIn("--build-path", commands[0])
        self.assertIn("button_events", commands[0])
        self.assertIn("rgb_blink", commands[1])
        self.assertIn("[PASS] arduino:rgb_blink", output)
        self.assertIn("[PASS] arduino:button_events", output)

    def test_single_sketch_argument_selects_one(self) -> None:
        code, output, runner = self._run_cli(["arduino", "rgb_blink"])
        self.assertEqual(code, 0, output)
        self.assertEqual(len(runner.commands()), 1)
        self.assertIn("rgb_blink", runner.commands()[0])
        self.assertNotIn("[FAIL]", output)

    def test_unknown_sketch_is_usage_error(self) -> None:
        code, output, _ = self._run_cli(["arduino", "nope"])
        self.assertEqual(code, 3)
        self.assertIn("unknown sketch", output)

    def test_staging_failure_fails_the_run(self) -> None:
        (self.root / "components" / "cb_bsp" / "src" / "cb_math.c").unlink()
        code, output, _ = self._run_cli(["arduino"])
        self.assertEqual(code, 1)
        self.assertIn("[FAIL] arduino:rgb_blink", output)
        self.assertIn("staging failed", output)

    def test_upload_with_two_named_sketches_is_exit_three(self) -> None:
        # The memory-mandated negative: two sketches can never be one upload. The port
        # guard runs first (AR13), so simulate a healthy board to reach the count check.
        import cb as cli

        saved_guard = cli.ports_mod.guard_port
        cli.ports_mod.guard_port = lambda explicit=None: "/dev/ttyACM0"  # type: ignore[assignment]
        try:
            code, output, runner = self._run_cli(["arduino", "--upload", "rgb_blink", "button_events"])
        finally:
            cli.ports_mod.guard_port = saved_guard  # type: ignore[assignment]
        self.assertEqual(code, 3)
        self.assertIn("exactly one sketch", output)
        self.assertIn("button_events, rgb_blink", output)
        self.assertEqual(runner.commands(), [], "nothing may run before the guard resolves")

    def test_upload_with_two_discovered_sketches_is_exit_three(self) -> None:
        import cb as cli

        saved_guard = cli.ports_mod.guard_port
        cli.ports_mod.guard_port = lambda explicit=None: "/dev/ttyACM0"  # type: ignore[assignment]
        try:
            code, output, _ = self._run_cli(["arduino", "--upload"])
        finally:
            cli.ports_mod.guard_port = saved_guard  # type: ignore[assignment]
        self.assertEqual(code, 3)
        self.assertIn("exactly one sketch, got 2", output)

    def test_upload_with_zero_sketches_is_exit_three(self) -> None:
        # Round-2 B1: --upload with zero discovered sketches is a usage error, never a
        # silent success — but the port guard still runs first (AR13).
        import cb as cli

        shutil.rmtree(self.root / "arduino")
        code, output, runner = self._run_cli(["arduino", "--upload"])  # no board (setUp)
        self.assertEqual(code, 2)
        self.assertIn("BOOT", output)
        self.assertEqual(runner.commands(), [])
        saved_guard = cli.ports_mod.guard_port
        cli.ports_mod.guard_port = lambda explicit=None: "/dev/ttyACM0"  # type: ignore[assignment]
        try:
            code, output, runner = self._run_cli(["arduino", "--upload"])
        finally:
            cli.ports_mod.guard_port = saved_guard  # type: ignore[assignment]
        self.assertEqual(code, 3)
        self.assertIn("exactly one sketch, got 0", output)
        self.assertIn("none discovered", output)
        self.assertEqual(runner.commands(), [], "nothing may run before the guard resolves")

    def test_upload_guards_the_port_before_building(self) -> None:
        # AR13: no board -> exit 2 + BOOT/RESET hint, and no build output at all.
        code, output, runner = self._run_cli(["arduino", "--upload", "rgb_blink"])
        self.assertEqual(code, 2)
        self.assertIn("BOOT", output)
        self.assertIn("RESET", output)
        self.assertEqual(runner.commands(), [])
        self.assertFalse(self.build_root.exists(), "guard must fire before any staging/build")

    def test_upload_compiles_then_uploads_the_build_path_output(self) -> None:
        import cb as cli

        saved_guard = cli.ports_mod.guard_port
        cli.ports_mod.guard_port = lambda explicit=None: "/dev/ttyACM0"  # type: ignore[assignment]
        try:
            code, output, runner = self._run_cli(["arduino", "--upload", "rgb_blink"])
        finally:
            cli.ports_mod.guard_port = saved_guard  # type: ignore[assignment]
        self.assertEqual(code, 0, output)
        commands = runner.commands()
        self.assertEqual(len(commands), 2, commands)
        self.assertIn("compile", commands[0])
        upload = commands[1]
        self.assertIn("upload", upload)
        self.assertIn("-p /dev/ttyACM0", upload)
        self.assertIn("--fqbn", upload)
        self.assertIn(f"--input-dir {self.build_root / 'arduino' / 'rgb_blink'}", upload)
        self.assertIn(str(self.root / "arduino" / "sketches" / "rgb_blink"), upload)

    def test_upload_failure_fails_the_run(self) -> None:
        import cb as cli

        class UploadFailRunner(FakeRunner):
            def run_arduino(self, argv, *, cwd, timeout=1200.0):  # type: ignore[override]
                result = super().run_arduino(argv, cwd=cwd)
                if "upload" in argv:
                    from cbchecks.proc import Result

                    return Result(list(argv), 1, "", "esptool: nope", 1)
                return result

        saved_guard = cli.ports_mod.guard_port
        cli.ports_mod.guard_port = lambda explicit=None: "/dev/ttyACM0"  # type: ignore[assignment]
        try:
            code, output, _ = self._run_cli(["arduino", "--upload", "rgb_blink"], runner_cls=UploadFailRunner)
        finally:
            cli.ports_mod.guard_port = saved_guard  # type: ignore[assignment]
        self.assertEqual(code, 1)
        self.assertIn("[FAIL] upload arduino:rgb_blink", output)

    def test_arduino_install_copies_the_staged_tree_into_the_sketchbook(self) -> None:
        import cb as cli

        sketchbook = self.root / "sketchbook"
        os.environ["ARDUINO_SKETCHBOOK"] = str(sketchbook)
        saved_runner = cli.Runner
        cli.Runner = lambda _ctx: FakeRunner(ctx_for(self.root))  # type: ignore[assignment]
        try:
            out = io.StringIO()
            with use_repo(self.root), redirect_stdout(out):
                code = cli.main(["arduino-install"])
        finally:
            cli.Runner = saved_runner  # type: ignore[assignment]
            os.environ.pop("ARDUINO_SKETCHBOOK", None)
        self.assertEqual(code, 0, out.getvalue())
        installed = sketchbook / "libraries" / "CBBoard" / "src"
        for rel in ARDUINO_MIRROR:
            self.assertEqual(
                (installed / Path(rel).name).read_bytes(),
                (self.root / "components" / "cb_bsp" / rel).read_bytes(),
                rel,
            )
        self.assertTrue((installed / "CBBoard.h").is_file())
        self.assertIn("staged", out.getvalue())

    def test_arduino_install_without_the_tree_is_an_env_error(self) -> None:
        import cb as cli

        shutil.rmtree(self.root / "arduino")
        os.environ["CB_BUILD_ROOT"] = str(self.root / "build/x")
        err = io.StringIO()
        with use_repo(self.root), redirect_stderr(err):
            code = cli.main(["arduino-install"])
        self.assertEqual(code, 3)
        self.assertIn("missing", err.getvalue())


class TestDoctorEscalation(TempRepoTest):
    """AR14: arduino-cli and the esp32:esp32 3.3.12 core are fatal once arduino/ exists."""

    def _doctor(self, ctx: Ctx, runner: FakeRunner) -> list[doctor.Check]:
        return doctor.run_doctor(ctx, runner)

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_missing_core_is_fatal_with_the_arduino_tree(self) -> None:
        runner = FakeRunner(self.ctx)
        runner.reply("arduino-cli version", 0, "arduino-cli 1.4.1\n", "")
        runner.reply("core list", 0, "arduino:avr 1.8.7\n", "")  # no esp32:esp32 3.3.12
        checks = self._doctor(self.ctx, runner)
        ard = next(c for c in checks if c.id == "arduino-cli")
        self.assertEqual(ard.status, "FAIL")
        self.assertTrue(ard.fatal)
        self.assertIn("not found", ard.detail)
        self.assertFalse(doctor.doctor_ok(checks))

    def test_missing_cli_is_fatal_with_the_arduino_tree(self) -> None:
        saved = doctor.arduino_cli
        doctor.arduino_cli = lambda: None  # type: ignore[assignment]
        try:
            checks = self._doctor(self.ctx, FakeRunner(self.ctx))
        finally:
            doctor.arduino_cli = saved  # type: ignore[assignment]
        ard = next(c for c in checks if c.id == "arduino-cli")
        self.assertEqual(ard.status, "FAIL")
        self.assertTrue(ard.fatal)

    def test_good_toolchain_passes(self) -> None:
        runner = FakeRunner(self.ctx)
        runner.reply("arduino-cli version", 0, "arduino-cli 1.4.1\n", "")
        runner.reply("core list", 0, "esp32:esp32 3.3.12 3.3.12 esp32\n", "")
        runner.reply("lib list", 0, "", "")
        checks = self._doctor(self.ctx, runner)
        ard = next(c for c in checks if c.id == "arduino-cli")
        self.assertEqual(ard.status, "PASS")
        self.assertIn("3.3.12", ard.detail)
        self.assertTrue(doctor.doctor_ok(checks))

    def test_without_the_arduino_tree_it_stays_a_warning(self) -> None:
        runner = FakeRunner(self.ctx)
        runner.reply("arduino-cli version", 0, "arduino-cli 1.4.1\n", "")
        runner.reply("core list", 0, "", "")
        shutil.rmtree(self.root / "arduino")
        checks = self._doctor(self.ctx, runner)
        ard = next(c for c in checks if c.id == "arduino-cli")
        self.assertEqual(ard.status, "WARN")
        self.assertFalse(ard.fatal)
        self.assertTrue(doctor.doctor_ok(checks))

    def test_no_arduino_flag_defangs_the_check(self) -> None:
        runner = FakeRunner(self.ctx)
        runner.reply("arduino-cli version", 0, "arduino-cli 1.4.1\n", "")
        runner.reply("core list", 0, "", "")
        ctx = ctx_for(self.root, no_arduino=True)
        checks = self._doctor(ctx, runner)
        ard = next(c for c in checks if c.id == "arduino-cli")
        self.assertEqual(ard.status, "WARN")
        self.assertFalse(ard.fatal)
        self.assertTrue(doctor.doctor_ok(checks))


class TestSketchDiscoveryBoundaries(TempRepoTest):
    """The staged tree never feeds sketch discovery; only committed sketches count."""

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_staging_does_not_create_sketch_targets(self) -> None:
        before = {t.name for t in targets.discover(self.ctx) if t.kind == "arduino"}
        staged, err = arduino.stage_library(self.ctx)
        self.assertIsNone(err)
        after = {t.name for t in targets.discover(self.ctx) if t.kind == "arduino"}
        self.assertEqual(before, {"rgb_blink", "button_events"})
        self.assertEqual(before, after)
        assert staged is not None
        self.assertFalse(list(staged.rglob("*.ino")))

    def test_library_dir_itself_is_not_a_sketch(self) -> None:
        # Even a stray .ino-less dir under libraries must not become a target.
        _write(self.root / "arduino" / "libraries" / "CBBoard" / "README.md", "docs\n")
        names = {t.name for t in targets.discover(self.ctx) if t.kind == "arduino"}
        self.assertEqual(names, {"rgb_blink", "button_events"})


if __name__ == "__main__":
    unittest.main()
