"""Shared helpers for ./cb unit tests.

Standard library only. Every test builds its own throwaway repo tree in a temp directory, so the
tests never depend on peers' files, on a board, or on the network.
"""

from __future__ import annotations

import contextlib
import os
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from cbchecks.context import Ctx  # noqa: E402
from cbchecks.proc import Result, Runner  # noqa: E402


class FakeRunner(Runner):
    """Records invocations and returns canned results; never spawns a process."""

    def __init__(self, ctx: Ctx, default: tuple[int, str, str] = (0, "", "")):
        super().__init__(ctx)
        self.calls: list[list[str]] = []
        self.cwd: list[Path | None] = []
        self.responses: dict[str, tuple[int, str, str]] = {}
        self._default = default
        self._offline_override: bool | None = None

    # -- canned replies ------------------------------------------------------
    def reply(self, needle: str, code: int, out: str = "", err: str = "") -> None:
        self.responses[needle] = (code, out, err)

    def _lookup(self, argv: list[str]) -> tuple[int, str, str]:
        joined = " ".join(argv)
        for needle, reply in self.responses.items():
            if needle in joined:
                return reply
        return self._default

    # -- Runner API ---------------------------------------------------------
    def run(self, argv, *, cwd=None, env_extra=None, timeout=300.0, prefix=None, use_lock=False):  # type: ignore[override]
        argv = list(argv)
        self.calls.append(argv)
        self.cwd.append(Path(cwd) if cwd else None)
        code, out, err = self._lookup(argv)
        return Result(argv, code, out, err, 1)

    def run_idf(self, args, *, cwd, timeout=3600.0):  # type: ignore[override]
        return self.run(["idf.py", *args], cwd=cwd)

    def run_make(self, argv, *, cwd, timeout=900.0):  # type: ignore[override]
        return self.run(list(argv), cwd=cwd)

    def run_arduino(self, argv, *, cwd, timeout=1200.0):  # type: ignore[override]
        return self.run(["arduino-cli", *argv], cwd=cwd)

    def idf_version(self) -> str:
        return "ESP-IDF v6.1"

    def offline_available(self) -> bool:
        if self._offline_override is not None:
            return self._offline_override
        return super().offline_available()

    def force_offline_available(self, value: bool) -> None:
        self._offline_override = value

    # -- helpers ------------------------------------------------------------
    def commands(self) -> list[str]:
        return [" ".join(call) for call in self.calls]

    def spawned_any(self, *needles: str) -> bool:
        return any(needle in cmd for cmd in self.commands() for needle in needles)


def make_repo(tmp: Path, *, app: bool = True, examples: tuple[str, ...] = ("01_lcd_hello",),
              arduino: bool = False, host_tests: bool = True) -> Path:
    """Create a minimal fake repository tree (no build, no network)."""
    tmp.mkdir(parents=True, exist_ok=True)
    (tmp / "LICENSE").write_text("test\n", encoding="utf-8")
    (tmp / "eim_config.toml").write_text('path = "/tmp/fake"\n', encoding="utf-8")
    if app:
        (tmp / "CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.16)\n", encoding="utf-8")
        (tmp / "main").mkdir(exist_ok=True)
        (tmp / "main" / "CMakeLists.txt").write_text(
            'idf_component_register(SRCS "app_main.c" INCLUDE_DIRS ".")\n', encoding="utf-8"
        )
        (tmp / "main" / "app_main.c").write_text("void app_main(void){}\n", encoding="utf-8")
        (tmp / "main" / "sdkconfig.app.defaults").write_text(
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions.csv"\n', encoding="utf-8"
        )
    for name in examples:
        base = tmp / "examples" / name / "main"
        base.mkdir(parents=True, exist_ok=True)
        (tmp / "examples" / name / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.16)\n", encoding="utf-8"
        )
        (base / "CMakeLists.txt").write_text(
            'idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")\n', encoding="utf-8"
        )
        (base / "main.c").write_text("void app_main(void){}\n", encoding="utf-8")
        (base / "sdkconfig.defaults").write_text(
            'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../config/partitions.csv"\n', encoding="utf-8"
        )
    if host_tests:
        host = tmp / "tests" / "host"
        host.mkdir(parents=True, exist_ok=True)
        (host / "Makefile").write_text("test:\n\t@true\n", encoding="utf-8")
    if arduino:
        sketch = tmp / "arduino" / "libraries" / "CBBoard" / "examples" / "01_smoke"
        sketch.mkdir(parents=True, exist_ok=True)
        (sketch / "01_smoke.ino").write_text("void setup(){}\nvoid loop(){}\n", encoding="utf-8")
        (tmp / "arduino" / "libraries" / "CBBoard" / "src").mkdir(parents=True, exist_ok=True)
        (tmp / "arduino" / "libraries" / "CBBoard" / "src" / "cb_pins.h").write_text(
            "#define CB_PIN_RGB 38\n", encoding="utf-8"
        )
    (tmp / ".git").mkdir(exist_ok=True)
    return tmp


@contextlib.contextmanager
def use_repo(root: Path):
    """Point the CLI at a throwaway repo tree (the CLI derives its root from context.repo_root())."""
    import cbchecks.context as ctx_mod

    original = ctx_mod.repo_root
    ctx_mod.repo_root = lambda: root  # type: ignore[assignment]
    try:
        yield
    finally:
        ctx_mod.repo_root = original  # type: ignore[assignment]


def ctx_for(root: Path, build_root: str = "build/agent-x", **kwargs) -> Ctx:
    os.environ["CB_BUILD_ROOT"] = build_root
    return Ctx(root=root, build_root=root / build_root, **kwargs)


class TempRepoTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "repo"
        self._saved_env = {k: os.environ.get(k) for k in ("CB_BUILD_ROOT", "CB_FORCE_NO_PORTS", "CB_IDF_ACTIVATE")}

    def tearDown(self) -> None:
        for key, value in self._saved_env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        self._tmp.cleanup()
