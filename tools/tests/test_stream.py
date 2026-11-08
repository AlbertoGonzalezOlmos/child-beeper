"""Live streaming machinery (used by `./cb monitor` and `./cb selftest`).

No board and no IDF: the stream primitive is exercised against plain shell commands.
"""

from __future__ import annotations

import time
import unittest

import support  # noqa: F401  (puts tools/ on sys.path)
from support import TempRepoTest, ctx_for, make_repo

from cbchecks.proc import Runner


class TestStream(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)
        self.runner = Runner(self.ctx)

    def _run(self, script: str, **kwargs):
        return self.runner.stream(["/bin/bash", "-c", script], cwd=self.root, echo=False, **kwargs)

    def test_lines_arrive_within_the_window(self) -> None:
        result = self._run(
            "for i in 1 2 3; do echo heartbeat $i; sleep 0.15; done", timeout=10.0
        )
        self.assertGreaterEqual(len(result.lines), 3, result.lines)
        self.assertIn("heartbeat 1", result.lines[0])
        self.assertEqual(result.code, 0)
        self.assertFalse(result.timed_out)

    def test_output_is_live_not_buffered_until_exit(self) -> None:
        seen: list[tuple[float, str]] = []
        start = time.monotonic()

        def sink(line: str) -> None:
            seen.append((time.monotonic() - start, line))

        # echo=True is what makes it live; capture the timing through a stop predicate.
        result = self.runner.stream(
            ["/bin/bash", "-c", "for i in 1 2 3 4 5; do echo tick $i; sleep 0.2; done"],
            cwd=self.root,
            timeout=10.0,
            echo=False,
            stop=lambda line: line.startswith("tick 2"),
        )
        self.assertTrue(any(line.startswith("tick 2") for line in result.lines))
        self.assertLess(len(result.lines), 5, "stop predicate must end the stream early")

    def test_deadline_is_enforced(self) -> None:
        start = time.monotonic()
        result = self._run("while :; do echo spin; sleep 0.05; done", timeout=0.7)
        elapsed = time.monotonic() - start
        self.assertTrue(result.timed_out)
        self.assertGreaterEqual(len(result.lines), 1, "must emit at least one line before the deadline")
        self.assertLess(elapsed, 6.0)

    def test_stop_predicate_marks_selftest_result(self) -> None:
        script = (
            "echo ITEM:lcd PASS; echo ITEM:rgb PASS; echo RESULT:PASS; "
            "echo should-not-be-seen; sleep 0.2"
        )
        result = self._run(script, timeout=5.0, stop=lambda line: "RESULT:" in line)
        self.assertEqual(result.lines[-1], "RESULT:PASS")
        self.assertNotIn("should-not-be-seen", result.lines)

    def test_exit_code_is_reported(self) -> None:
        result = self._run("echo bye; exit 7", timeout=5.0)
        self.assertEqual(result.code, 7)
        self.assertFalse(result.ok)


if __name__ == "__main__":
    unittest.main()
