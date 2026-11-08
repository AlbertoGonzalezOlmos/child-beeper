"""Subprocess execution: IDF/Arduino/make wrappers, repo-level flock, offline namespace.

Rulings honoured here:
  R8  --offline uses `unshare -rn`; IDF_COMPONENT_MANAGER is never touched.
  R9  every IDF/make/arduino-cli invocation is serialised by a flock at <build_root>/.cb.lock.
"""

from __future__ import annotations

import contextlib
import fcntl
import os
import queue
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence

from .context import Ctx, arduino_cli, require_idf_activate
from .errors import BuildError, EnvError


@dataclass
class Result:
    argv: list[str]
    code: int
    out: str
    err: str
    duration_ms: int

    @property
    def ok(self) -> bool:
        return self.code == 0

    def tail(self, n: int = 12) -> str:
        text = (self.err or "") + (self.out or "")
        lines = [ln for ln in text.splitlines() if ln.strip()]
        return "\n".join(lines[-n:])


def _now_ms() -> int:
    return int(time.monotonic() * 1000)


class Runner:
    """Runs commands; test doubles subclass this and record calls."""

    def __init__(self, ctx: Ctx):
        self.ctx = ctx
        self._offline_prefix: list[str] | None = None
        self._unshare_probed = False

    # ---------------------------------------------------------------- offline
    def offline_available(self) -> bool:
        if not self.ctx.offline:
            return False
        if self._offline_prefix is None:
            self._unshare_probed = True
            try:
                probe = subprocess.run(
                    ["unshare", "-rn", "true"], capture_output=True, text=True, timeout=20
                )
                self._offline_prefix = ["unshare", "-rn"] if probe.returncode == 0 else []
            except (OSError, subprocess.SubprocessError):
                self._offline_prefix = []
        return bool(self._offline_prefix)

    def _prefix(self) -> list[str]:
        if not self.ctx.offline:
            return []
        return list(self._offline_prefix or [])

    # -------------------------------------------------------------- locking
    @contextlib.contextmanager
    def _lock(self):
        # R9: repo-level lock — per-build-root locks let two agents race on the shared
        # project dirs (managed_components/, dependencies.lock, sdkconfig).
        lock_dir = self.ctx.root / ".cache"
        lock_dir.mkdir(parents=True, exist_ok=True)
        lock_path = lock_dir / "cb-build.lock"
        with open(lock_path, "w") as handle:
            try:
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                print("cb: waiting for build lock (another agent is building)…", file=sys.stderr)
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(handle.fileno(), fcntl.LOCK_UN)

    # ---------------------------------------------------------------- runner
    def run(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None = None,
        env_extra: dict[str, str] | None = None,
        timeout: float = 300.0,
        prefix: Sequence[str] | None = None,
        use_lock: bool = False,
    ) -> Result:
        argv = list(prefix or []) + list(argv)
        env = dict(os.environ)
        env.setdefault("GIT_TERMINAL_PROMPT", "0")
        if env_extra:
            env.update(env_extra)
        start = _now_ms()
        try:
            proc = subprocess.run(
                argv,
                cwd=str(cwd) if cwd else None,
                env=env,
                capture_output=True,
                text=True,
                errors="replace",
                timeout=timeout,
            )
            result = Result(argv, proc.returncode, proc.stdout or "", proc.stderr or "", _now_ms() - start)
        except subprocess.TimeoutExpired as exc:
            out = exc.stdout or ""
            err = exc.stderr or ""
            if isinstance(out, bytes):
                out = out.decode("utf-8", "replace")
            if isinstance(err, bytes):
                err = err.decode("utf-8", "replace")
            result = Result(argv, 124, out, err + f"\ncb: timed out after {timeout:.0f}s", _now_ms() - start)
        except FileNotFoundError as exc:
            raise EnvError(f"command not found: {argv[0]} ({exc})") from exc
        return result

    # ------------------------------------------------------------------ IDF
    # The activation script infers "sourced vs executed" from ${0##*/}, so the bash -c argv0 slot
    # must be a shell name ("bash"), never our program name.
    SHELL_ARGV0 = "bash"

    def _idf_script(self) -> str:
        script = require_idf_activate()
        return f'set -e; . "{script}" >/dev/null 2>&1; exec idf.py "$@"'

    def idf_argv(self, args: Sequence[str]) -> list[str]:
        """argv that runs idf.py inside the activated ESP-IDF environment."""
        return ["bash", "-lc", self._idf_script(), self.SHELL_ARGV0, *args]

    def run_idf(self, args: Sequence[str], *, cwd: Path, timeout: float = 3600.0) -> Result:
        prefix = self._prefix()
        with self._lock():
            return self.run(
                ["bash", "-lc", self._idf_script(), self.SHELL_ARGV0, *args],
                cwd=cwd,
                timeout=timeout,
                prefix=prefix,
                use_lock=True,
            )

    def idf_version(self) -> str:
        result = self.run_idf(["--version"], cwd=self.ctx.root, timeout=180.0)
        if not result.ok:
            raise EnvError((result.tail(6) or "idf.py --version failed").strip())
        return (result.out or result.err).strip().splitlines()[-1] if (result.out or result.err).strip() else ""

    # ----------------------------------------------------------------- make
    def run_make(self, argv: Sequence[str], *, cwd: Path, timeout: float = 900.0) -> Result:
        env_extra = {"CB_BUILD_ROOT": os.environ.get("CB_BUILD_ROOT", str(self.ctx.build_root))}
        with self._lock():
            return self.run(argv, cwd=cwd, env_extra=env_extra, timeout=timeout, prefix=self._prefix())

    # -------------------------------------------------------------- arduino
    def run_arduino(self, argv: Sequence[str], *, cwd: Path, timeout: float = 1200.0) -> Result:
        cli = arduino_cli()
        if cli is None:
            raise EnvError("arduino-cli not found (set $CB_ARDUINO_CLI or install it)")
        with self._lock():
            return self.run([str(cli), *argv], cwd=cwd, timeout=timeout, prefix=self._prefix())

    # ---------------------------------------------------------- IDF shell snippet
    def run_idf_snippet(self, snippet: str, *, cwd: Path, timeout: float = 180.0) -> Result:
        """Run a shell snippet after activating IDF (e.g. gen_esp32part.py)."""
        script = f'set -e; . "{require_idf_activate()}" >/dev/null 2>&1; {snippet}'
        return self.run(["bash", "-lc", script], cwd=cwd, timeout=timeout, prefix=self._prefix())

    # -------------------------------------------------------------- streaming
    @dataclass
    class StreamResult:
        lines: list[str]
        code: int | None
        timed_out: bool

        @property
        def ok(self) -> bool:
            return not self.timed_out and self.code in (0, None)

    def stream(
        self,
        argv: Sequence[str],
        *,
        cwd: Path | None = None,
        timeout: float = 60.0,
        echo: bool = True,
        env_extra: dict[str, str] | None = None,
        stop: "Callable[[str], bool] | None" = None,
        use_lock: bool = False,
    ):
        """Run argv, forwarding stdout line by line (live), with a deadline and optional stop.

        Writes are flushed immediately, so this is genuinely live even through a pipe.
        stdin is inherited (so idf.py monitor keeps working: Ctrl-] exits it).
        """
        argv = list(argv)
        env = dict(os.environ)
        env.setdefault("PYTHONUNBUFFERED", "1")
        if env_extra:
            env.update(env_extra)
        started = _now_ms()
        lock_cm = self._lock() if use_lock else contextlib.nullcontext()
        with lock_cm:
            proc = subprocess.Popen(
                argv,
                cwd=str(cwd) if cwd else None,
                env=env,
                stdin=None,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                bufsize=1,
            )
            lines: list[str] = []
            timed_out = False
            assert proc.stdout is not None
            reader_done = threading.Event()

            def _pump() -> None:
                try:
                    for raw in proc.stdout:  # type: ignore[union-attr]
                        q.put(raw)
                finally:
                    reader_done.set()

            q: "queue.Queue[str]" = queue.Queue()
            thread = threading.Thread(target=_pump, daemon=True)
            thread.start()
            try:
                while True:
                    remaining = timeout - (_now_ms() - started) / 1000.0
                    if remaining <= 0:
                        timed_out = True
                        break
                    try:
                        line = q.get(timeout=min(0.25, remaining))
                    except queue.Empty:
                        if reader_done.is_set() and proc.poll() is not None:
                            break
                        continue
                    text = line.rstrip("\r\n")
                    lines.append(text)
                    if echo:
                        sys.stdout.write(text + "\n")
                        sys.stdout.flush()
                    if stop is not None and stop(text):
                        break
            finally:
                if proc.poll() is None:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                reader_done.wait(timeout=2)
                try:
                    proc.stdout.close()
                except OSError:
                    pass
            return Runner.StreamResult(lines, proc.returncode, timed_out)

    def monitor_live(self, args: Sequence[str], *, cwd: Path, timeout: float) -> "Runner.StreamResult":
        """`idf.py monitor` with live output; Ctrl-] (handled by idf.py) exits."""
        return self.stream(self.idf_argv(args), cwd=cwd, timeout=timeout, echo=True)
