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

# Post-item-7 Arduino tree (AR8/AR16): the frozen 12-entry mirror set and the two
# sketches. Keep in sync with arduino/libraries/CBBoard/mirror.txt.
ARDUINO_MIRROR = (
    "include/cb_pins.h",
    "include/cb_math.h",
    "include/cb_button_fsm.h",
    "include/cb_qmi8658_regs.h",
    "include/cb_rgb_math.h",
    "include/cb_battery_math.h",
    "include/cb_imu_math.h",
    "src/cb_math.c",
    "src/cb_button_fsm.c",
    "src/cb_rgb_math.c",
    "src/cb_battery_math.c",
    "src/cb_imu_math.c",
)
ARDUINO_SKETCHES = ("rgb_blink", "button_events")


def make_arduino_tree(tmp: Path) -> None:
    """The committed post-item-7 arduino/ tree: umbrella library + two sketches.

    No mirror file is ever committed under arduino/ (AR1/AR2) — the canonical pure
    modules live under components/cb_bsp and are staged by cbchecks.arduino at build
    time; pin-drift byte-compares the stage against them.
    """
    lib = tmp / "arduino" / "libraries" / "CBBoard"
    lib.mkdir(parents=True, exist_ok=True)
    (lib / "CBBoard.h").write_text(
        "#ifndef CBBOARD_H\n#define CBBOARD_H\n"
        + "".join(
            f'#include "{Path(rel).name}"\n' for rel in ARDUINO_MIRROR if rel.startswith("include/")
        )
        + "#endif /* CBBOARD_H */\n",
        encoding="utf-8",
    )
    (lib / "library.properties").write_text(
        "name=CBBoard\nversion=0.1.0\narchitectures=esp32\nincludes=CBBoard.h\n",
        encoding="utf-8",
    )
    (lib / "mirror.txt").write_text(
        "# fixture mirror manifest (AR8)\n" + "\n".join(ARDUINO_MIRROR) + "\n",
        encoding="utf-8",
    )
    for name in ARDUINO_SKETCHES:
        sketch = tmp / "arduino" / "sketches" / name
        sketch.mkdir(parents=True, exist_ok=True)
        (sketch / f"{name}.ino").write_text(
            f"#include <CBBoard.h>\nvoid setup() {{}}\nvoid loop() {{}}\n",
            encoding="utf-8",
        )
    for rel in ARDUINO_MIRROR:
        canonical = tmp / "components" / "cb_bsp" / rel
        canonical.parent.mkdir(parents=True, exist_ok=True)
        canonical.write_text(f"/* fixture {rel} */\n", encoding="utf-8")


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


def make_repo(
    tmp: Path,
    *,
    app: bool = True,
    # Default mirrors the post-item-6 tree: six example projects (FIX-BRIEF round 2).
    examples: tuple[str, ...] = (
        "01_lcd_hello",
        "02_rgb_button",
        "03_battery",
        "04_imu",
        "05_sdcard",
        "06_dashboard",
    ),
    # Default mirrors the post-item-7 tree: the CBBoard library + two sketches (AR16).
    arduino: bool = True,
    host_tests: bool = True,
) -> Path:
    """Create a minimal fake repository tree (no build, no network)."""
    # deps-pins fixtures: exact pins so the happy path is green and negatives can be applied.
    lock = (
        "dependencies:\n"
        "  cb_bsp:\n"
        "    source:\n"
        "      path: components/cb_bsp\n"
        "      type: local\n"
        "    version: '*'\n"
        "  espressif/esp_lvgl_port:\n"
        "    dependencies:\n"
        "    - name: lvgl/lvgl\n"
        "      version: '>=8,<10'\n"
        "    version: 2.9.0\n"
        "  espressif/led_strip:\n"
        "    version: 3.1.1\n"
        "  idf:\n"
        "    version: 6.1.0\n"
        "  lvgl/lvgl:\n"
        "    version: 9.5.0\n"
    )
    manifest = (
        "dependencies:\n"
        "  idf: '>=6.1.0,<6.2.0'\n"
        "  lvgl/lvgl: '9.5.0'\n"
        "  espressif/esp_lvgl_port: '2.9.0'\n"
        "  espressif/led_strip: '3.1.1'\n"
    )
    lock_dir_root = tmp
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
        (tmp / "examples" / name).mkdir(parents=True, exist_ok=True)
        (tmp / "examples" / name / "dependencies.lock").write_text(lock, encoding="utf-8")
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
    (tmp / "tools" / "tests").mkdir(parents=True, exist_ok=True)
    (tmp / "dependencies.lock").write_text(lock, encoding="utf-8")
    (tmp / "components" / "cb_bsp").mkdir(parents=True, exist_ok=True)
    (tmp / "components" / "cb_bsp" / "idf_component.yml").write_text(manifest, encoding="utf-8")
    if host_tests:
        host = tmp / "tests" / "host"
        host.mkdir(parents=True, exist_ok=True)
        (host / "Makefile").write_text("test:\n\t@true\n", encoding="utf-8")
    if arduino:
        make_arduino_tree(tmp)
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
        self._saved_env = {k: os.environ.get(k) for k in
                           ("CB_BUILD_ROOT", "CB_FORCE_NO_PORTS", "CB_IDF_ACTIVATE", "CB_ARDUINO_CLI")}
        # FakeRunner supplies tool output; discovery must not require real tools
        # on the host running these unit tests.
        for key, name in (("CB_IDF_ACTIVATE", "activate-idf.sh"), ("CB_ARDUINO_CLI", "arduino-cli")):
            fixture = Path(self._tmp.name) / name
            fixture.write_text("# fixture: execution is handled by FakeRunner\n", encoding="utf-8")
            os.environ[key] = str(fixture)

    def tearDown(self) -> None:
        for key, value in self._saved_env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        self._tmp.cleanup()
