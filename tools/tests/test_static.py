"""Static audits: forbidden constructs, absolute home paths, pin drift."""

from __future__ import annotations

import os
import unittest

import support
from support import TempRepoTest, ctx_for, make_repo

from cbchecks import staticchecks


def _write(path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class TestForbiddenConstructs(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_clean_tree_has_no_findings(self) -> None:
        self.assertEqual(staticchecks.scan_forbidden(self.ctx), [])

    def test_legacy_i2c_header_is_flagged(self) -> None:
        legacy = "driver/i2c" + ".h"
        _write(self.root / "main" / "bad.c", f'#include "{legacy}"\n')
        rules = [f.rule for f in staticchecks.scan_forbidden(self.ctx)]
        self.assertTrue(any("driver/i2c.h" in rule for rule in rules), rules)

    def test_lvgl8_and_arduino2_symbols_are_flagged(self) -> None:
        _write(
            self.root / "examples/01_lcd_hello/main/lcd.c",
            "static " + "lv_disp" + "_drv_t drv;\n"
            "void f(void){ " + "ledc" + "Setup(1,2,3); }\n",
        )
        rules = " ".join(f.rule for f in staticchecks.scan_forbidden(self.ctx))
        self.assertIn("lv_disp_drv_t", rules)
        self.assertIn("ledcSetup", rules)

    def test_sd_autoformat_is_flagged(self) -> None:
        _write(
            self.root / "components/cb_bsp/src/cb_sd.c",
            "cfg.format_if_mount" + "_failed = true;\n",
        )
        rules = " ".join(f.rule for f in staticchecks.scan_forbidden(self.ctx))
        self.assertIn("format_if_mount_failed", rules)

    def test_esp_error_check_inside_bsp_is_flagged_but_allowable(self) -> None:
        bsp_dir = self.root / "components/cb_bsp/src"
        _write(bsp_dir / "cb_rgb.c", "void f(void){ ESP_ERROR" + "_CHECK(1); }\n")
        findings = staticchecks.scan_forbidden(self.ctx)
        self.assertTrue(any("ESP_ERROR_CHECK" in f.rule for f in findings))
        _write(bsp_dir / "cb_rgb.c", "void f(void){ ESP_ERROR" + "_CHECK(1); } // cbcheck:allow mandatory\n")
        findings = staticchecks.scan_forbidden(self.ctx)
        self.assertFalse(any("ESP_ERROR_CHECK" in f.rule for f in findings))

    def test_docs_are_exempt_from_construct_rules(self) -> None:
        _write(self.root / "docs" / "troubleshooting.md", "Never use `" + "driver/i2c" + ".h`.\n")
        _write(self.root / "README.md", "Do not use `" + "lv_disp" + "_drv_t`.\n")
        self.assertEqual(staticchecks.scan_forbidden(self.ctx), [])

    def test_allow_file_marker_is_honoured(self) -> None:
        _write(
            self.root / "components" / "x" / "lint_table.c",
            "/* cbcheck:allow-file: rule catalogue */\n" + 'x = "' + "driver/i2c" + '.h";' + "\n",
        )
        self.assertEqual(staticchecks.scan_forbidden(self.ctx), [])


class TestAbsolutePaths(TempRepoTest):
    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)

    def test_home_path_in_a_source_is_flagged(self) -> None:
        home_path = "/" + "home/someone/x"
        _write(self.root / "main" / "app_main.c", f'const char *p = "{home_path}";\n')
        findings = staticchecks.scan_abs_paths(self.ctx)
        self.assertEqual(len(findings), 1)
        self.assertIn("main/app_main.c", findings[0].path)

    def test_prose_about_hardcoding_home_paths_is_not_flagged(self) -> None:
        # Docs legitimately write `/home/…` (or ~/...) when telling readers NOT to hardcode paths.
        _write(self.root / "AGENTS.md", "- never hardcode `/" + "home/…`; use $HOME.\n")
        self.assertEqual(staticchecks.scan_abs_paths(self.ctx), [])

    def test_eim_config_is_allowlisted(self) -> None:
        _write(self.root / "eim_config.toml", 'idf_path = "/' + 'home/someone/.espressif"\n')
        self.assertEqual(staticchecks.scan_abs_paths(self.ctx), [])

    def test_build_and_git_dirs_are_skipped(self) -> None:
        home_path = "/" + "home/someone/x"
        _write(self.root / "build" / "log.txt", home_path + "\n")
        _write(self.root / ".git" / "config", home_path + "\n")
        self.assertEqual(staticchecks.scan_abs_paths(self.ctx), [])


class TestPinDrift(TempRepoTest):
    def test_skips_when_arduino_tree_is_absent(self) -> None:
        make_repo(self.root)
        status, detail = staticchecks.pin_drift(ctx_for(self.root))
        self.assertEqual(status, "SKIP")
        self.assertIn("arduino", detail)

    def test_byte_equal_copy_passes(self) -> None:
        make_repo(self.root, arduino=True)
        canonical = self.root / staticchecks.BSP_PINS
        _write(canonical, "#define CB_PIN_RGB 38\n")
        copy = self.root / "arduino/libraries/CBBoard/src/cb_pins.h"
        copy.write_bytes(canonical.read_bytes())
        status, _ = staticchecks.pin_drift(ctx_for(self.root))
        self.assertEqual(status, "PASS")

    def test_mutated_copy_fails(self) -> None:
        make_repo(self.root, arduino=True)
        canonical = self.root / staticchecks.BSP_PINS
        _write(canonical, "#define CB_PIN_RGB 38\n")
        _write(self.root / "arduino/libraries/CBBoard/src/cb_pins.h", "#define CB_PIN_RGB 39\n")
        status, detail = staticchecks.pin_drift(ctx_for(self.root))
        self.assertEqual(status, "FAIL")
        self.assertIn("differs", detail)


if __name__ == "__main__":
    unittest.main()
