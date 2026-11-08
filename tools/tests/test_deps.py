"""Dependency-pin assertion (ITEM2 ruling A9): lock files + component manifest."""
from __future__ import annotations

import unittest
from pathlib import Path

import support
from support import TempRepoTest, ctx_for, make_repo

from cbchecks import deps

LOCK_OK = """dependencies:
  cb_bsp:
    dependencies:
    - name: idf
      version: '>=6.1.0,<6.2.0'
    source:
      path: components/cb_bsp
      type: local
    version: '*'
  espressif/esp_lvgl_port:
    dependencies:
    - name: lvgl/lvgl
      require: public
      version: '>=8,<10'
    version: 2.9.0
  espressif/led_strip:
    version: 3.1.1
  idf:
    source:
      type: idf
    version: 6.1.0
  lvgl/lvgl:
    dependencies: []
    version: 9.5.0
"""

MANIFEST_OK = """dependencies:
  idf: '>=6.1.0,<6.2.0'
  lvgl/lvgl: '9.5.0'
  espressif/esp_lvgl_port: '2.9.0'
  espressif/led_strip: '3.1.1'
"""


def _write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class TestDepsPins(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def _install_ok(self) -> None:
        _write(self.root / "dependencies.lock", LOCK_OK)
        _write(self.root / "examples/01_lcd_hello/dependencies.lock", LOCK_OK)
        _write(self.root / "components/cb_bsp/idf_component.yml", MANIFEST_OK)

    def test_passes_with_exact_pins(self) -> None:
        self._install_ok()
        ok, detail, extra = deps.check_pins(self.ctx)
        self.assertTrue(ok, detail)
        self.assertIn("lvgl/lvgl 9.5.0", detail)
        self.assertEqual(len(extra["locks"]), 2)

    def test_wrong_lock_version_fails_naming_file(self) -> None:
        self._install_ok()
        _write(self.root / "examples/01_lcd_hello/dependencies.lock", LOCK_OK.replace("2.9.0", "2.8.0"))
        ok, detail, extra = deps.check_pins(self.ctx)
        self.assertFalse(ok)
        self.assertIn("examples/01_lcd_hello/dependencies.lock", detail)
        self.assertIn("esp_lvgl_port is 2.8.0, expected 2.9.0", detail)
        self.assertTrue(extra["problems"])

    def test_transitive_range_is_not_mistaken_for_the_pin(self) -> None:
        self._install_ok()
        # the lock contains `version: '>=8,<10'` for lvgl inside esp_lvgl_port's dependency list
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertTrue(ok, detail)

    def test_missing_lock_for_a_project_fails(self) -> None:
        self._install_ok()
        (self.root / "examples/01_lcd_hello/dependencies.lock").unlink()
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertFalse(ok)
        self.assertIn("examples/01_lcd_hello/dependencies.lock", detail)
        self.assertIn("./cb deps", detail)

    def test_manifest_range_pin_fails(self) -> None:
        self._install_ok()
        _write(
            self.root / "components/cb_bsp/idf_component.yml",
            MANIFEST_OK.replace("'9.5.0'", "'^9.3.0'"),
        )
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertFalse(ok)
        self.assertIn("idf_component.yml", detail)
        self.assertIn("expected 9.5.0", detail)

    def test_missing_manifest_fails(self) -> None:
        self._install_ok()
        (self.root / "components/cb_bsp/idf_component.yml").unlink()
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertFalse(ok)
        self.assertIn("idf_component.yml", detail)

    def test_unparsable_lock_fails_loudly(self) -> None:
        self._install_ok()
        _write(self.root / "dependencies.lock", "this is not yaml at all\n")
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertFalse(ok)
        self.assertIn("cannot parse", detail)

    def test_managed_components_and_build_dirs_are_ignored(self) -> None:
        self._install_ok()
        _write(self.root / "managed_components/foo/dependencies.lock", "garbage\n")
        _write(self.root / "build/x/dependencies.lock", "garbage\n")
        ok, detail, _ = deps.check_pins(self.ctx)
        self.assertTrue(ok, detail)


if __name__ == "__main__":
    unittest.main()
