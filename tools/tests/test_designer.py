"""Design validation, pin protection, asset budgets and generated firmware data."""
from __future__ import annotations

import base64
import copy
import io
import tempfile
import unittest
from pathlib import Path

from tools.designer import compiler, schema

try:
    from PIL import Image, ImageFont
except ImportError:
    Image = ImageFont = None


class TestDesignerSchema(unittest.TestCase):
    def setUp(self):
        self.project = schema.default_project()

    def test_starter_is_valid_and_all_external_outputs_are_disabled(self):
        p = schema.normalize({"version": 1, "name": "Starter"})
        self.assertEqual(len(p["layers"]), 24)
        self.assertFalse(p["hardware"]["motor"]["enabled"])
        self.assertFalse(p["hardware"]["audio"]["speaker"])
        self.assertFalse(p["hardware"]["audio"]["microphone"])
        self.assertTrue(all(not b["enabled"] for b in p["hardware"]["buttons"]))

    def test_board_usb_strapping_and_non_header_pins_are_rejected(self):
        for pin in range(49):
            p = copy.deepcopy(self.project)
            p["hardware"]["motor"].update(enabled=True, gpio=pin)
            if pin in schema.GPIO_PINS:
                schema.normalize(p)
            else:
                with self.subTest(pin=pin), self.assertRaises(schema.DesignError):
                    schema.normalize(p)

    def test_nine_spare_pins_can_support_all_future_peripherals(self):
        h = self.project["hardware"]
        for b in h["buttons"]:
            b["enabled"] = True
        h["motor"]["enabled"] = h["audio"]["speaker"] = h["audio"]["microphone"] = True
        schema.normalize(self.project)
        h["audio"]["din"] = h["motor"]["gpio"]
        with self.assertRaisesRegex(schema.DesignError, "already assigned"):
            schema.normalize(self.project)

    def test_numeric_booleans_and_fractional_positions_are_rejected(self):
        for bad in (True, 1.5, "12", None):
            p = copy.deepcopy(self.project)
            p["layers"][0]["x"] = bad
            with self.subTest(value=bad), self.assertRaises(schema.DesignError):
                schema.normalize(p)

    def test_animation_order_and_first_key_are_validated(self):
        animation = self.project["layers"][0]["animation"]
        animation["keys"] = [dict(at=20, x=0, y=0, opacity=255)]
        with self.assertRaisesRegex(schema.DesignError, "0 ms"):
            schema.normalize(self.project)
        animation["keys"] = [dict(at=0, x=0, y=0, opacity=255)] * 2
        with self.assertRaisesRegex(schema.DesignError, "increasing"):
            schema.normalize(self.project)

    def test_effect_budget_and_empty_sequences_are_rejected(self):
        effect = self.project["effects"]["alarm"]
        effect["steps"] = []
        with self.assertRaises(schema.DesignError):
            schema.normalize(self.project)
        effect["steps"] = [schema.step(5000)] * 13
        with self.assertRaisesRegex(schema.DesignError, "60 seconds"):
            schema.normalize(self.project)

    def test_unknown_assets_and_malformed_data_cannot_reach_codegen(self):
        self.project["layers"][0].update(kind="image", asset="../../wifi_settings.txt")
        with self.assertRaises(schema.DesignError):
            schema.normalize(self.project)
        self.project = schema.default_project()
        self.project["assets"] = [dict(id="bad", name="bad", kind="image", data="data:image/png;base64,???")]
        with self.assertRaises(schema.DesignError):
            schema.normalize(self.project)

    def test_c_strings_cannot_inject_source(self):
        label = '"; malicious(); /* \\\n café'
        self.project["layers"][0]["text"] = label
        _, header, _, _ = compiler.compile_project(self.project)
        self.assertIn(compiler.c_string(label), header)
        self.assertNotIn('.text=""; malicious()', header)

    def test_unsupported_versions_and_control_characters_rejected(self):
        for source in ({"version": 2}, {"version": True}, {"version": 1, "name": "bad\0name"}):
            with self.assertRaises(schema.DesignError):
                schema.normalize(source)

    def test_default_codegen_has_three_sequences_and_disabled_routes(self):
        _, header, storage, report = compiler.compile_project(self.project)
        self.assertEqual(storage, {})
        self.assertEqual(report["storageBytes"], 0)
        self.assertIn(".motor_gpio=-1,.speaker=false,.microphone=false", header)
        for name in ("idle", "message", "alarm"):
            self.assertIn("effect_" + name, header)

    def test_compiler_does_not_touch_outputs_if_validation_fails(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "out"
            storage = Path(folder) / "storage"
            compiler.write_compiled(self.project, output, storage)
            original = (output / "design_data.h").read_bytes()
            self.project["hardware"]["motor"].update(enabled=True, gpio=19)
            with self.assertRaises(schema.DesignError):
                compiler.write_compiled(self.project, output, storage)
            self.assertEqual((output / "design_data.h").read_bytes(), original)


@unittest.skipIf(Image is None, "Run with .cache/designer/venv/bin/python for Pillow asset checks")
class TestDesignerAssets(unittest.TestCase):
    def image_asset(self, image, kind="PNG"):
        data = io.BytesIO()
        image.save(data, format=kind)
        return dict(id="image1", name="Image", kind="image", width=image.width, height=image.height,
                    data=f"data:image/{kind.lower()};base64," + base64.b64encode(data.getvalue()).decode())

    def test_rgb565_alpha_planes_and_transparency(self):
        image = Image.new("RGBA", (2, 1))
        image.putdata([(255, 0, 0, 255), (0, 0, 255, 0)])
        self.assertEqual(compiler.frame_bytes(image), b"\x00\xf8\x1f\x00\xff\x00")

    def test_gif_frames_keep_distinct_pixels_and_clamped_durations(self):
        data = io.BytesIO()
        frames = [Image.new("RGB", (4, 4), color) for color in ("red", "blue")]
        frames[0].save(data, format="GIF", save_all=True, append_images=frames[1:], duration=[20, 150], loop=0)
        asset = dict(id="anim", name="Animation", kind="image", width=4, height=4,
                     data="data:image/gif;base64," + base64.b64encode(data.getvalue()).decode())
        images, durations = compiler.decode_image(asset)
        self.assertEqual(durations, [50, 150])
        self.assertNotEqual(compiler.frame_bytes(images[0]), compiler.frame_bytes(images[1]))

    def test_font_compiler_and_preview_agree_on_metrics(self):
        font = ImageFont.load_default(size=20)
        asset = dict(id="font1", name="Test", kind="font", size=20, characters="",
                     data="data:font/ttf;base64," + base64.b64encode(font.path.getvalue()).decode())
        p = schema.default_project()
        p["assets"] = [asset]
        p["layers"][0]["font"] = asset["id"]
        normalized, header, _, report = compiler.compile_project(p)
        data = compiler.custom_font(normalized["assets"][0], normalized)
        preview = compiler.font_preview(data)
        self.assertEqual(preview["lineHeight"], data["lineHeight"])
        self.assertGreater(preview["glyphs"][65]["advance"], 0)
        self.assertIn(".cmap_num=1, .bpp=4", header)
        self.assertGreater(report["fontBytes"], 1000)

    def test_changed_assets_remove_only_owned_stale_frame_files(self):
        p = schema.default_project()
        p["assets"] = [self.image_asset(Image.new("RGBA", (3, 3), "red"))]
        with tempfile.TemporaryDirectory() as folder:
            output, storage = Path(folder) / "out", Path(folder) / "storage"
            compiler.write_compiled(p, output, storage)
            self.assertEqual(len(list(storage.glob("a*.bin"))), 1)
            (storage / "keep.txt").write_text("unrelated")
            p["assets"] = []
            compiler.write_compiled(p, output, storage)
            self.assertEqual(list(storage.glob("a*.bin")), [])
            self.assertEqual((storage / "keep.txt").read_text(), "unrelated")
