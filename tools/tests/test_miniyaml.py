"""Mini YAML subset parser used for dependencies.lock / idf_component.yml."""
from __future__ import annotations

import unittest

import support  # noqa: F401  (puts tools/ on sys.path)

from cbchecks import miniyaml


class TestMiniYaml(unittest.TestCase):
    def test_nested_map_and_scalars(self) -> None:
        text = "a:\n  b: 1\n  c: 'two'\n"
        self.assertEqual(miniyaml.parse(text), {"a": {"b": "1", "c": "two"}})

    def test_empty_flow_collections(self) -> None:
        self.assertEqual(miniyaml.parse("a: []\nb: {}\n"), {"a": [], "b": {}})

    def test_comment_and_quote_handling(self) -> None:
        text = "# header\na: 'x # not a comment'\nb: y # trailing\n"
        self.assertEqual(miniyaml.parse(text), {"a": "x # not a comment", "b": "y"})

    def test_list_at_parent_column(self) -> None:
        # ESP-IDF writes `dependencies:` followed by `- name:` at the SAME indentation.
        text = (
            "dependencies:\n"
            "  comp:\n"
            "    dependencies:\n"
            "    - name: idf\n"
            "      version: '>=5.2'\n"
            "    version: 2.9.0\n"
        )
        parsed = miniyaml.parse(text)
        self.assertEqual(parsed["dependencies"]["comp"]["version"], "2.9.0")
        self.assertEqual(parsed["dependencies"]["comp"]["dependencies"][0]["name"], "idf")

    def test_list_of_scalars(self) -> None:
        self.assertEqual(miniyaml.parse("a:\n- x\n- y\n"), {"a": ["x", "y"]})

    def test_component_version_is_not_the_transitive_range(self) -> None:
        text = (
            "dependencies:\n"
            "  espressif/esp_lvgl_port:\n"
            "    dependencies:\n"
            "    - name: lvgl/lvgl\n"
            "      require: public\n"
            "      version: '>=8,<10'\n"
            "    version: 2.9.0\n"
        )
        parsed = miniyaml.parse(text)
        self.assertEqual(parsed["dependencies"]["espressif/esp_lvgl_port"]["version"], "2.9.0")

    def test_get_path(self) -> None:
        data = {"a": {"b": {"c": "d"}}}
        self.assertEqual(miniyaml.get_path(data, "a.b.c"), "d")
        self.assertIsNone(miniyaml.get_path(data, "a.x.c"))

    def test_junk_raises(self) -> None:
        with self.assertRaises(ValueError):
            miniyaml.parse("no colon here\n")
        with self.assertRaises(ValueError):
            miniyaml.parse("  indented: start\n")

    def test_real_committed_locks_resolve_exact_pins(self) -> None:
        # Strongest fixture available: the repo's own committed lock files.
        from pathlib import Path

        root = Path(__file__).resolve().parents[2]
        for rel in ("dependencies.lock", "examples/01_lcd_hello/dependencies.lock"):
            path = root / rel
            if not path.is_file():
                continue
            deps = miniyaml.parse(path.read_text())["dependencies"]
            self.assertEqual(deps["lvgl/lvgl"]["version"], "9.5.0", rel)
            self.assertEqual(deps["espressif/esp_lvgl_port"]["version"], "2.9.0", rel)
            self.assertEqual(deps["espressif/led_strip"]["version"], "3.1.1", rel)
            self.assertEqual(deps["idf"]["version"], "6.1.0", rel)


if __name__ == "__main__":
    unittest.main()
