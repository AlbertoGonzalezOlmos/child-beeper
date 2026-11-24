"""Static audits: forbidden constructs, absolute home paths, LCD invariants.

The Arduino pin-drift tests live in test_arduino.py (item-7 AR10: cbchecks.arduino
owns the real staged-vs-canonical byte compare).
"""

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


    def test_sketches_under_arduino_are_scanned_too(self) -> None:
        # AR11: audit-static gains arduino/ as a scanned tree — .ino files obey the rules.
        _write(
            self.root / "arduino" / "sketches" / "bad" / "bad.ino",
            "void loop(){ neo" + "pixelWrite(38, 8, 0, 0); }\n",
        )
        rules = " ".join(f.rule for f in staticchecks.scan_forbidden(self.ctx))
        self.assertIn("neopixelWrite", rules)


class TestLcdInvariants(TempRepoTest):
    """Item-2 hardware-proven invariants inside cb_lcd.c (io_handle must be set; no PSRAM DMA bufs)."""

    GOOD = (
        "static void f(void)\n"
        "{\n"
        "    const lvgl_port_display_cfg_t cfg = {\n"
        "        .io_handle = s_io_handle,\n"
        "        .panel_handle = s_panel,\n"
        "        .flags = { .buff_dma = true, .buff_spiram = false, .swap_bytes = true },\n"
        "    };\n"
        "}\n"
    )

    def setUp(self) -> None:
        super().setUp()
        make_repo(self.root)
        self.ctx = ctx_for(self.root)
        self.path = self.root / staticchecks.LCD_INVARIANTS_FILE
        self.path.parent.mkdir(parents=True, exist_ok=True)

    def test_good_file_passes(self) -> None:
        self.path.write_text(self.GOOD, encoding="utf-8")
        self.assertEqual(staticchecks.scan_lcd_invariants(self.ctx), [])

    def test_missing_io_handle_is_flagged(self) -> None:
        self.path.write_text(
            self.GOOD.replace(".io_handle = s_io_handle,\n", ""), encoding="utf-8"
        )
        findings = staticchecks.scan_lcd_invariants(self.ctx)
        self.assertTrue(any("io_handle" in f.rule for f in findings), [f.render() for f in findings])

    def test_null_io_handle_is_flagged(self) -> None:
        self.path.write_text(
            self.GOOD.replace(".io_handle = s_io_handle,", ".io_handle = NULL,"), encoding="utf-8"
        )
        findings = staticchecks.scan_lcd_invariants(self.ctx)
        self.assertTrue(any("io_handle" in f.rule for f in findings), [f.render() for f in findings])

    def test_buff_spiram_true_is_flagged(self) -> None:
        self.path.write_text(
            self.GOOD.replace(".buff_spiram = false", ".buff_spiram = true"), encoding="utf-8"
        )
        findings = staticchecks.scan_lcd_invariants(self.ctx)
        self.assertTrue(any("buff_spiram" in f.rule for f in findings), [f.render() for f in findings])

    def test_absent_file_is_not_a_finding(self) -> None:
        self.assertEqual(staticchecks.scan_lcd_invariants(self.ctx), [])

    def test_audit_static_reports_the_findings(self) -> None:
        self.path.write_text(
            self.GOOD.replace(".buff_spiram = false", ".buff_spiram = true"), encoding="utf-8"
        )
        status, detail = staticchecks.audit_static(self.ctx)
        self.assertEqual(status, "FAIL")
        self.assertIn("buff_spiram", detail)


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
        # Docs legitimately reference the forbidden pattern when telling readers NOT to hardcode.
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


if __name__ == "__main__":
    unittest.main()
