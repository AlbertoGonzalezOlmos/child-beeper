"""The frozen selftest serial grammar (item 6, ITEM6-BRIEF ST2 + ITEM6-RULINGS).

`_parse_selftest` is exercised against the RULINGS gold table byte-exact (with and
without IDF log prefixes / ANSI escapes) plus the full malformed-output matrix:
a golden PASS run, a RESULT:FAIL run with one failing item, and one case per
ruled rule violation. Static string vectors only — no board, no port, no network
(ST8 / ITEM6-RULINGS "Tests").
"""

from __future__ import annotations

import unittest

import support  # noqa: F401  (puts tools/ on sys.path)
from cb import _parse_selftest, _selftest_target_name

# ITEM6-RULINGS "serial contract" — THE gold standard (byte-exact, sums to 7).
GOLD = [
    "SELFTEST BEGIN version=1 cb_bsp=0.5.0",
    "ITEM:lifecycle PASS init=ESP_OK",
    "ITEM:lcd PASS lvgl=9.5.0 frames=1",
    "ITEM:rgb PASS cap_percent=20",
    "ITEM:button SKIP reason=needs-human poller=running",
    "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1",
    "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
    "ITEM:sd SKIP reason=no-card",
    "RESULT:PASS total=7 pass=5 fail=0 skip=2",
]

GOLD_FAIL = [
    "SELFTEST BEGIN version=1 cb_bsp=0.5.0",
    "ITEM:lifecycle PASS init=ESP_OK",
    "ITEM:lcd PASS lvgl=9.5.0 frames=1",
    "ITEM:rgb PASS cap_percent=20",
    "ITEM:button SKIP reason=needs-human poller=running",
    "ITEM:battery FAIL battery_mv=1200 pin_mv=398 calibrated=1 plausible=0",
    "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
    "ITEM:sd SKIP reason=no-card",
    "RESULT:FAIL total=7 pass=4 fail=1 skip=2",
]


def _gold() -> list[str]:
    return list(GOLD)


def _replace(lines: list[str], old: str, new: str) -> list[str]:
    out = []
    for ln in lines:
        out.append(new if ln == old else ln)
    return out


class TestParseHappy(unittest.TestCase):
    def _parse(self, lines):
        return _parse_selftest(lines)

    def test_golden_pass_table(self) -> None:
        ok, detail, skipped = self._parse(GOLD)
        self.assertTrue(ok, detail)
        self.assertEqual(detail, "RESULT:PASS total=7 pass=5 fail=0 skip=2")
        self.assertEqual(skipped, 2)

    def test_golden_with_idf_prefix_ansi_and_noise(self) -> None:
        ms = 600
        lines = [
            "I (120) cb_bsp: LCD: init ok",
            "I (565) cb_sd: mount failed: ESP_ERR_TIMEOUT (no card?)",
        ]
        for ln in GOLD:
            lines.append(f"\x1b[0;32mI ({ms}) dashboard: {ln}\x1b[0m")
            ms += 7
        lines.append("\x1b[0;32mI (2000) dashboard: alive #1\x1b[0m")
        lines.append("I (2002) dashboard: live bat=4221mV plausible=1 imu_mag=0.99g addr=0x6B")
        ok, detail, skipped = self._parse(lines)
        self.assertTrue(ok, detail)
        self.assertEqual(detail, "RESULT:PASS total=7 pass=5 fail=0 skip=2")
        self.assertEqual(skipped, 2)

    def test_result_fail_lists_failing_item(self) -> None:
        ok, detail, skipped = self._parse(GOLD_FAIL)
        self.assertFalse(ok)
        self.assertIn("1 failing item(s): battery", detail)
        self.assertIn("RESULT:FAIL total=7 pass=4 fail=1 skip=2", detail)
        self.assertEqual(skipped, 2)

    def test_sd_absent_skip_reason_is_legal(self) -> None:
        # Round 2: `absent` is legal on sd only; a SKIP anywhere else is a FAIL.
        lines = _replace(GOLD, "ITEM:sd SKIP reason=no-card", "ITEM:sd SKIP reason=absent")
        ok, detail, skipped = self._parse(lines)
        self.assertTrue(ok, detail)
        self.assertEqual(skipped, 2)

    def test_lines_after_result_without_markers_are_ignored(self) -> None:
        lines = GOLD + [
            "I (2100) dashboard: live bat=4221mV plausible=1 imu_mag=0.99g addr=0x6B",
            "I (5000) dashboard: alive #1",
        ]
        ok, _, skipped = self._parse(lines)
        self.assertTrue(ok)
        self.assertEqual(skipped, 2)


class TestParseMalformed(unittest.TestCase):
    """One case per ruled rule violation (ITEM6-RULINGS "Tests" list + extras)."""

    def _reject(self, lines, rule):
        ok, detail, skipped = _parse_selftest(lines)
        self.assertFalse(ok, f"unexpectedly accepted: {detail}")
        self.assertEqual(skipped, 0)
        self.assertIn(rule, detail)
        return detail

    def test_bad_version_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "SELFTEST BEGIN version=1 cb_bsp=0.5.0",
                     "SELFTEST BEGIN version=2 cb_bsp=0.5.0"),
            "unsupported selftest version=2 (expected version=1)",
        )

    def test_six_items_rejected(self) -> None:
        lines = [ln for ln in GOLD if ln != "ITEM:sd SKIP reason=no-card"]
        self._reject(lines, "expected 7 ITEM lines, saw 6")

    def test_eight_items_rejected(self) -> None:
        lines = GOLD[:-1] + ["ITEM:sd SKIP reason=no-card", GOLD[-1]]
        self._reject(lines, "expected RESULT after 7 items")

    def test_out_of_order_tokens_rejected(self) -> None:
        rgb = "ITEM:rgb PASS cap_percent=20"
        btn = "ITEM:button SKIP reason=needs-human poller=running"
        lines = list(GOLD)
        i, j = lines.index(rgb), lines.index(btn)
        lines[i], lines[j] = lines[j], lines[i]
        self._reject(lines, "item out of order: expected 'rgb', saw 'button'")

    def test_missing_reason_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:sd SKIP reason=no-card", "ITEM:sd SKIP"),
            "SKIP item without reason= as the first field",
        )

    def test_skip_with_pass_text_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:rgb PASS cap_percent=20",
                     "ITEM:rgb SKIP PASS reason=absent"),
            "malformed field 'PASS'",
        )

    def test_arithmetic_mismatch_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "RESULT:PASS total=7 pass=5 fail=0 skip=2",
                     "RESULT:PASS total=7 pass=4 fail=0 skip=2"),
            "arithmetic mismatch: pass+fail+skip=6, expected 7",
        )

    def test_result_not_last_rejected(self) -> None:
        lines = GOLD[:-1] + ["RESULT:PASS total=7 pass=5 fail=0 skip=2",
                             "ITEM:sd SKIP reason=no-card"]
        self._reject(lines, "contract line after RESULT")

    def test_second_result_rejected(self) -> None:
        lines = GOLD + ["RESULT:FAIL total=7 pass=6 fail=1 skip=0"]
        self._reject(lines, "second RESULT line after the terminal RESULT")

    # ---- extras, each tracing to a ruled rule ------------------------------

    def test_no_begin_rejected(self) -> None:
        self._reject(GOLD[1:], "no SELFTEST BEGIN line within timeout")

    def test_contract_line_before_begin_rejected(self) -> None:
        lines = ["ITEM:lifecycle PASS init=ESP_OK"] + GOLD
        self._reject(lines, "contract line before SELFTEST BEGIN")

    def test_malformed_begin_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "SELFTEST BEGIN version=1 cb_bsp=0.5.0",
                     "SELFTEST BEGIN version=1"),
            "malformed SELFTEST BEGIN line",
        )

    def test_unknown_item_token_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:lifecycle PASS init=ESP_OK",
                     "ITEM:wifi PASS rssi=-40"),
            "unknown item token 'wifi'",
        )

    def test_malformed_item_line_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:lcd PASS lvgl=9.5.0 frames=1",
                     "ITEM:lcd MAYBE frames=1"),
            "malformed ITEM line",
        )

    def test_pass_without_evidence_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:rgb PASS cap_percent=20", "ITEM:rgb PASS"),
            "PASS item without key=value evidence",
        )

    def test_malformed_field_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:rgb PASS cap_percent=20",
                     "ITEM:rgb PASS cap_percent"),
            "malformed field 'cap_percent'",
        )

    def test_button_pass_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button PASS poller=running"),
            "button must be SKIP reason=needs-human, saw status PASS",
        )

    def test_button_fail_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button FAIL poller=stopped"),
            "button must be SKIP reason=needs-human, saw status FAIL",
        )

    def test_button_wrong_reason_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button SKIP reason=no-card poller=running"),
            "button SKIP reason must be needs-human",
        )

    def test_illegal_skip_reason_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:sd SKIP reason=no-card",
                     "ITEM:sd SKIP reason=bogus"),
            "illegal SKIP reason 'bogus'",
        )

    def test_skip_on_non_sd_item_rejected(self) -> None:
        # Round 2: absence of lifecycle/lcd/rgb/battery/imu hardware is a FAIL,
        # never a SKIP — even with a would-be-legal reason token.
        self._reject(
            _replace(GOLD, "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
                     "ITEM:imu SKIP reason=absent"),
            "SKIP is not legitimate for 'imu'",
        )

    def test_total_not_seven_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "RESULT:PASS total=7 pass=5 fail=0 skip=2",
                     "RESULT:PASS total=6 pass=5 fail=0 skip=2"),
            "total=6, expected 7",
        )

    def test_count_tally_mismatch_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "RESULT:PASS total=7 pass=5 fail=0 skip=2",
                     "RESULT:PASS total=7 pass=6 fail=0 skip=1"),
            "count mismatch: items say PASS=5 FAIL=0 SKIP=2, RESULT says pass=6 fail=0 skip=1",
        )

    def test_result_pass_with_fail_rejected(self) -> None:
        self._reject(
            _replace(GOLD_FAIL, "RESULT:FAIL total=7 pass=4 fail=1 skip=2",
                     "RESULT:PASS total=7 pass=4 fail=1 skip=2"),
            "RESULT:PASS with fail=1 > 0",
        )

    def test_result_fail_with_zero_fails_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "RESULT:PASS total=7 pass=5 fail=0 skip=2",
                     "RESULT:FAIL total=7 pass=5 fail=0 skip=2"),
            "RESULT:FAIL with fail=0 (verdict must follow the items)",
        )

    def test_no_result_rejected(self) -> None:
        self._reject(GOLD[:-1], "no RESULT: line within timeout (saw 7 item(s))")

    def test_malformed_result_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "RESULT:PASS total=7 pass=5 fail=0 skip=2",
                     "RESULT:PASS total=7"),
            "malformed RESULT line",
        )

    def test_stray_result_marker_in_noise_rejected(self) -> None:
        # ITEM6-RULINGS: any line CONTAINING RESULT: that fails the grammar is a
        # hard FAIL — a stray marker in noise must not be silently skipped.
        lines = GOLD[:7] + ["driver note mentions RESULT: oddly"] + GOLD[7:]
        self._reject(lines, "malformed RESULT line")

    # ---- round 2 (FIX-BRIEF-item6): one case per new strictness bullet ----

    def test_second_begin_rejected(self) -> None:
        lines = GOLD[:5] + ["SELFTEST BEGIN version=1 cb_bsp=0.5.0"] + GOLD[5:]
        self._reject(lines, "second SELFTEST BEGIN line")

    def test_button_skip_without_poller_running_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button SKIP reason=needs-human"),
            "button line must be exactly 'SKIP reason=needs-human poller=running'",
        )

    def test_button_skip_wrong_poller_field_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button SKIP reason=needs-human poller=stopped"),
            "button line must be exactly 'SKIP reason=needs-human poller=running'",
        )

    def test_button_skip_extra_field_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:button SKIP reason=needs-human poller=running",
                     "ITEM:button SKIP reason=needs-human poller=running extra=1"),
            "button line must be exactly 'SKIP reason=needs-human poller=running'",
        )

    def test_lifecycle_pass_missing_init_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:lifecycle PASS init=ESP_OK",
                     "ITEM:lifecycle PASS err=ESP_OK"),
            "lifecycle PASS missing required evidence key 'init'",
        )

    def test_lcd_pass_frames_zero_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:lcd PASS lvgl=9.5.0 frames=1",
                     "ITEM:lcd PASS lvgl=9.5.0 frames=0"),
            "lcd PASS has frames=0, expected 1",
        )

    def test_rgb_pass_wrong_cap_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:rgb PASS cap_percent=20",
                     "ITEM:rgb PASS cap_percent=100"),
            "rgb PASS has cap_percent=100, expected 20",
        )

    def test_battery_pass_calibrated_zero_rejected(self) -> None:
        # Round 2: a degraded (uncalibrated) reading is not a pass.
        self._reject(
            _replace(GOLD, "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1",
                     "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=0 plausible=1"),
            "battery PASS has calibrated=0, expected 1",
        )

    def test_battery_pass_plausible_zero_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1",
                     "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=0"),
            "battery PASS has plausible=0, expected 1",
        )

    def test_imu_pass_wrong_who_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
                     "ITEM:imu PASS addr=0x6B who=0x00 mag_g=0.99"),
            "imu PASS has who=0x00, expected 0x05",
        )

    def test_imu_pass_wrong_addr_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
                     "ITEM:imu PASS addr=0x6C who=0x05 mag_g=0.99"),
            "imu PASS has addr=0x6C, expected 0x6A|0x6B",
        )

    def test_imu_pass_missing_mag_g_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:imu PASS addr=0x6B who=0x05 mag_g=0.99",
                     "ITEM:imu PASS addr=0x6B who=0x05"),
            "imu PASS missing required evidence key 'mag_g'",
        )

    def test_duplicate_field_key_rejected(self) -> None:
        self._reject(
            _replace(GOLD, "ITEM:battery PASS battery_mv=4221 pin_mv=1397 calibrated=1 plausible=1",
                     "ITEM:battery PASS battery_mv=4221 battery_mv=4221 calibrated=1 plausible=1"),
            "duplicate key 'battery_mv'",
        )


class TestTargetChoice(unittest.TestCase):
    """ITEM6-RULINGS target preference: 06_dashboard, then 07, then 08."""

    def test_prefers_06_dashboard_over_all(self) -> None:
        self.assertEqual(
            _selftest_target_name({"08_selftest", "07_selftest", "06_dashboard", "app"}),
            "06_dashboard",
        )

    def test_prefers_07_over_08_when_06_absent(self) -> None:
        self.assertEqual(_selftest_target_name({"08_selftest", "07_selftest"}), "07_selftest")

    def test_accepts_08_when_only_candidate(self) -> None:
        self.assertEqual(_selftest_target_name({"08_selftest"}), "08_selftest")

    def test_none_when_no_selftest_firmware(self) -> None:
        self.assertIsNone(_selftest_target_name({"app", "01_lcd_hello"}))


if __name__ == "__main__":
    unittest.main()
