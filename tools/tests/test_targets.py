"""Target discovery, build-dir mapping, CB_BUILD_ROOT handling, clean safety."""

from __future__ import annotations

import io
import os
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import support
from support import FakeRunner, TempRepoTest, ctx_for, make_repo, use_repo

from cbchecks import targets
from cbchecks.errors import UsageError


class TestDiscovery(TempRepoTest):
    def test_finds_app_and_examples_in_sorted_order(self) -> None:
        make_repo(self.root, examples=("02_second", "01_lcd_hello"))
        ctx = ctx_for(self.root)
        found = targets.discover(ctx)
        idf = [t.name for t in found if t.is_idf]
        self.assertEqual(idf[0], "app")
        self.assertEqual(idf[1:], ["01_lcd_hello", "02_second"])

    def test_build_dirs_follow_cb_build_root(self) -> None:
        make_repo(self.root)
        ctx = ctx_for(self.root, "build/deepseek-flash")
        found = {t.name: t for t in targets.discover(ctx)}
        self.assertEqual(
            targets.build_dir(ctx, found["app"]), self.root / "build/deepseek-flash/app"
        )
        self.assertEqual(
            targets.build_dir(ctx, found["01_lcd_hello"]),
            self.root / "build/deepseek-flash/01_lcd_hello",
        )

    def test_absolute_build_root_is_respected(self) -> None:
        make_repo(self.root)
        absolute = self.root / "elsewhere" / "b"
        os.environ["CB_BUILD_ROOT"] = str(absolute)
        ctx = ctx_for(self.root, str(absolute))
        self.assertEqual(ctx.build_root, absolute)

    def test_arduino_sketches_are_discovered_and_skippable(self) -> None:
        make_repo(self.root, arduino=True)
        ctx = ctx_for(self.root)
        names = {t.name for t in targets.discover(ctx) if t.kind == "arduino"}
        self.assertEqual(names, {"01_smoke"})
        ctx_no_ard = ctx_for(self.root, no_arduino=True)
        self.assertFalse([t for t in targets.discover(ctx_no_ard) if t.kind == "arduino"])

    def test_unknown_target_lists_valid_names(self) -> None:
        make_repo(self.root)
        ctx = ctx_for(self.root)
        with self.assertRaises(UsageError) as raised:
            targets.resolve(ctx, ["nope"])
        self.assertIn("01_lcd_hello", str(raised.exception))
        self.assertIn("app", str(raised.exception))

    def test_default_target_is_app(self) -> None:
        make_repo(self.root)
        ctx = ctx_for(self.root)
        chosen = targets.resolve(ctx, [])
        self.assertEqual([t.name for t in chosen], ["app"])

    def test_all_expands_to_every_target(self) -> None:
        make_repo(self.root, arduino=True)
        ctx = ctx_for(self.root)
        chosen = targets.resolve(ctx, ["all"])
        self.assertEqual([t.name for t in chosen], ["app", "01_lcd_hello", "01_smoke"])


class TestCleanSafety(TempRepoTest):
    def _run_clean(self, ctx_root: Path, build_root: str, target: str | None, all_: bool) -> int:
        import cb as cli

        saved = os.environ.get("CB_BUILD_ROOT")
        os.environ["CB_BUILD_ROOT"] = build_root
        try:
            argv = ["clean"] + ([target] if target else []) + (["--all"] if all_ else [])
            out, err = io.StringIO(), io.StringIO()
            with use_repo(ctx_root), redirect_stdout(out), redirect_stderr(err):
                code = cli.main(argv)
            self._last_err = err.getvalue()
            return code
        finally:
            if saved is None:
                os.environ.pop("CB_BUILD_ROOT", None)
            else:
                os.environ["CB_BUILD_ROOT"] = saved

    def test_clean_refuses_paths_outside_the_repo(self) -> None:
        make_repo(self.root)
        code = self._run_clean(self.root, "/tmp", None, True)
        self.assertEqual(code, 3)
        self.assertIn("refusing", self._last_err)

    def test_clean_refuses_the_repo_root(self) -> None:
        make_repo(self.root)
        code = self._run_clean(self.root, ".", None, True)
        self.assertEqual(code, 3)

    def test_clean_removes_only_the_target_dir(self) -> None:
        make_repo(self.root)
        keep = self.root / "build/deepseek-flash/01_lcd_hello"
        drop = self.root / "build/deepseek-flash/app"
        keep.mkdir(parents=True)
        drop.mkdir(parents=True)
        code = self._run_clean(self.root, "build/deepseek-flash", "app", False)
        self.assertEqual(code, 0)
        self.assertTrue(keep.is_dir())
        self.assertFalse(drop.exists())


class TestCliSurface(TempRepoTest):
    def test_list_runs_without_targets(self) -> None:
        import cb as cli

        make_repo(self.root, app=False, examples=(), host_tests=False)
        os.environ["CB_BUILD_ROOT"] = str(self.root / "build/x")
        out = io.StringIO()
        with use_repo(self.root), redirect_stdout(out):
            code = cli.main(["list"])
        self.assertEqual(code, 0)
        self.assertIn("no targets discovered", out.getvalue())

    def test_bad_usage_returns_three(self) -> None:
        import cb as cli

        make_repo(self.root)
        os.environ["CB_BUILD_ROOT"] = str(self.root / "build/x")
        err = io.StringIO()
        with use_repo(self.root), redirect_stderr(err):
            code = cli.main(["build", "does_not_exist"])
        self.assertEqual(code, 3)
        self.assertIn("unknown target", err.getvalue())

    def test_test_command_without_host_tests_is_env_error(self) -> None:
        import cb as cli

        make_repo(self.root, host_tests=False)
        os.environ["CB_BUILD_ROOT"] = str(self.root / "build/x")
        err = io.StringIO()
        with use_repo(self.root), redirect_stderr(err):
            code = cli.main(["test"])
        self.assertEqual(code, 3)
        self.assertIn("tests/host/Makefile", err.getvalue())


if __name__ == "__main__":
    unittest.main()
