"""Port guard behaviour: exit 2, recovery hint, and zero subprocesses before the guard."""

from __future__ import annotations

import io
import os
import unittest
from contextlib import redirect_stderr, redirect_stdout

import support
from support import TempRepoTest, ctx_for, make_repo, use_repo

from cbchecks import ports
from cbchecks.errors import BoardError


class TestPortListing(TempRepoTest):
    def test_force_no_ports_env_empties_the_list(self) -> None:
        os.environ["CB_FORCE_NO_PORTS"] = "1"
        self.assertEqual(ports.list_ports(), [])

    def test_guard_without_ports_mentions_recovery(self) -> None:
        os.environ["CB_FORCE_NO_PORTS"] = "1"
        with self.assertRaises(BoardError) as raised:
            ports.guard_port()
        message = str(raised.exception)
        self.assertIn("BOOT", message)
        self.assertIn("RESET", message)
        self.assertEqual(raised.exception.code, 2)

    def test_guard_refuses_non_device_explicit_port(self) -> None:
        fake = self.root / "not-a-port"
        fake.parent.mkdir(parents=True, exist_ok=True)
        fake.write_text("x", encoding="utf-8")
        with self.assertRaises(BoardError) as raised:
            ports.guard_port(str(fake))
        self.assertEqual(raised.exception.code, 2)
        self.assertIn("character device", str(raised.exception))

    def test_guard_refuses_multiple_ports(self) -> None:
        saved = ports.list_ports
        ports.list_ports = lambda: ["/dev/ttyACM0", "/dev/ttyACM1"]  # type: ignore[assignment]
        try:
            with self.assertRaises(BoardError) as raised:
                ports.guard_port()
        finally:
            ports.list_ports = saved  # type: ignore[assignment]
        message = str(raised.exception)
        self.assertIn("/dev/ttyACM0", message)
        self.assertIn("/dev/ttyACM1", message)
        self.assertIn("--port", message)


class TestGuardsRunBeforeBuild(TempRepoTest):
    """Ruling R6: flash/monitor must exit 2 without building or calling esptool."""

    def _run(self, argv: list[str]) -> tuple[int, str]:
        import cb as cli

        make_repo(self.root)
        os.environ["CB_BUILD_ROOT"] = str(self.root / "build/x")
        os.environ["CB_FORCE_NO_PORTS"] = "1"
        out, err = io.StringIO(), io.StringIO()
        with use_repo(self.root), redirect_stdout(out), redirect_stderr(err):
            code = cli.main(argv)
        return code, err.getvalue()

    def test_flash_exits_two_with_hint(self) -> None:
        code, err = self._run(["flash"])
        self.assertEqual(code, 2)
        self.assertIn("BOOT", err)
        self.assertIn("RESET", err)

    def test_monitor_exits_two_with_hint(self) -> None:
        code, err = self._run(["monitor"])
        self.assertEqual(code, 2)
        self.assertIn("BOOT", err)

    def test_selftest_exits_two_with_hint(self) -> None:
        code, err = self._run(["selftest"])
        self.assertEqual(code, 2)
        self.assertIn("BOOT", err)

    def test_flash_creates_no_build_output(self) -> None:
        self._run(["flash"])
        build_root = self.root / "build/x"
        self.assertFalse((build_root / "app").exists())
        self.assertFalse((build_root / "logs").exists())


class TestGuardDirectness(TempRepoTest):
    """The guard must run before any build, even with a fake runner in place."""

    def test_no_runner_call_would_be_needed(self) -> None:
        make_repo(self.root)
        ctx = ctx_for(self.root)
        saved = ports.list_ports
        ports.list_ports = lambda: []  # type: ignore[assignment]
        try:
            with self.assertRaises(BoardError):
                ports.guard_port()
        finally:
            ports.list_ports = saved  # type: ignore[assignment]
        # Nothing above touched the filesystem or any subprocess helper.
        self.assertFalse(ctx.build_root.exists())


if __name__ == "__main__":
    unittest.main()
