"""Compile a validated project into C descriptors and bounded RGB565/A8 frames."""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
from pathlib import Path
import re
import tempfile
import warnings

try:
    from . import schema
except ImportError:  # CMake invokes this file directly.
    import schema

ROOT = Path(__file__).resolve().parents[2]
FONT_SYMBOLS = ("lv_font_montserrat_14", "lv_font_montserrat_20",
                "lv_font_montserrat_28", "lv_font_montserrat_32",
                "lv_font_unscii_8", "lv_font_unscii_16")


def pillow():
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        raise schema.DesignError("Image/GIF/font compilation needs Pillow. Run ./designer once to install it.") from None
    Image.MAX_IMAGE_PIXELS = 4_000_000
    return Image, ImageDraw, ImageFont


def decode_image(asset):
    Image, _, _ = pillow()
    frames, durations = [], []
    with warnings.catch_warnings():
        warnings.simplefilter("error", Image.DecompressionBombWarning)
        try:
            with Image.open(io.BytesIO(schema.asset_bytes(asset))) as image:
                if image.format not in ("PNG", "JPEG", "GIF", "WEBP"):
                    raise schema.DesignError("Use PNG, JPEG, GIF or WebP images.")
                count = getattr(image, "n_frames", 1)
                if count > 120:
                    raise schema.DesignError("A GIF/animated image can contain at most 120 frames.")
                if count * asset["width"] * asset["height"] * 3 > schema.MAX_STORAGE_BYTES:
                    raise schema.DesignError("Decoded animation exceeds the 6 MB asset budget. Reduce size or frames.")
                for index in range(count):
                    image.seek(index)
                    frame = image.convert("RGBA").resize(
                        (asset["width"], asset["height"]), Image.Resampling.LANCZOS)
                    frames.append(frame)
                    durations.append(max(50, min(5000, int(image.info.get("duration", 100) or 100))))
        except (OSError, ValueError, Image.DecompressionBombError, Image.DecompressionBombWarning) as error:
            raise schema.DesignError(f"Cannot decode {asset['name']}: {error}") from None
    return frames, durations


def image_preview(asset):
    frames, durations = decode_image(asset)
    result = []
    for image, duration in zip(frames, durations):
        data = io.BytesIO()
        image.save(data, format="PNG")
        result.append(dict(duration=duration,
                           data="data:image/png;base64," + base64.b64encode(data.getvalue()).decode()))
    return dict(width=asset["width"], height=asset["height"], frames=result)


def frame_bytes(image):
    """LVGL RGB565A8: little-endian color plane followed by one alpha byte/pixel."""
    count = image.width * image.height
    colors, alpha = bytearray(count * 2), bytearray(count)
    for index, (r, g, b, a) in enumerate(image.getdata()):
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        colors[index * 2] = value & 255
        colors[index * 2 + 1] = value >> 8
        alpha[index] = a
    return bytes(colors + alpha)


def c_string(value):
    # JSON's string escaping is also valid C for the accepted control characters.
    return json.dumps(value, ensure_ascii=False)


def c_bool(value):
    return "true" if value else "false"


def c_color(value):
    return "0x" + value[1:]


def numbers(values, width=24):
    values = list(values)
    return "\n".join("    " + ",".join(str(v) for v in values[i:i + width]) + ","
                     for i in range(0, len(values), width))


def custom_font(asset, project):
    Image, ImageDraw, ImageFont = pillow()
    try:
        font = ImageFont.truetype(io.BytesIO(schema.asset_bytes(asset)), asset["size"])
    except (OSError, ValueError):
        raise schema.DesignError(f"{asset['name']} is not a supported TrueType/OpenType font.") from None
    characters = set(range(32, 127)) | set(range(160, 256))
    extra = asset.get("characters", "") + "".join(
        layer["text"] for layer in project["layers"] if layer["font"] == asset["id"])
    if any(ord(char) > 0xFFFF for char in extra):
        raise schema.DesignError("Imported fonts support BMP characters (U+0020–U+FFFF), not emoji planes.")
    characters.update(ord(char) for char in extra if ord(char) >= 32)
    if len(characters) > 512:
        raise schema.DesignError("Each imported font supports at most 512 selected glyphs.")
    glyphs, bitmap = {}, bytearray()
    for code in sorted(characters):
        char = chr(code)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        w, h = right - left, bottom - top
        advance = round(font.getlength(char) * 16)
        if not (0 <= w <= 255 and 0 <= h <= 255 and -128 <= left <= 127
                and -128 <= -bottom <= 127 and 0 <= advance <= 4095):
            raise schema.DesignError("Font glyph metrics exceed the device format.")
        index = len(bitmap)
        pixels = []
        if w and h:
            image = Image.new("L", (w, h))
            ImageDraw.Draw(image).text((-left, -top), char, font=font, fill=255, anchor="ls")
            pixels = [round(value * 15 / 255) for value in image.getdata()]
            bitmap.extend((pixels[i] << 4) | (pixels[i + 1] if i + 1 < len(pixels) else 0)
                          for i in range(0, len(pixels), 2))
        glyphs[code] = dict(index=index, width=w, height=h, ox=left, oy=-bottom, advance=advance)
    ascent, descent = font.getmetrics()
    return dict(bitmap=bytes(bitmap), glyphs=glyphs, bpp=4,
                lineHeight=ascent + descent, baseline=descent)


def font_c(data, name):
    glyphs = data["glyphs"]
    codes = sorted(glyphs)
    entries = ["    {0},"]  # LVGL reserves glyph ID zero.
    for code in codes:
        g = glyphs[code]
        entries.append("    {.bitmap_index=%d,.adv_w=%d,.box_w=%d,.box_h=%d,.ofs_x=%d,.ofs_y=%d}," %
                       (g["index"], g["advance"], g["width"], g["height"], g["ox"], g["oy"]))
    return f"""
static const uint8_t {name}_bitmap[] = {{
{numbers(data['bitmap'])}
}};
static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[] = {{
{chr(10).join(entries)}
}};
static const uint16_t {name}_unicode[] = {{
{numbers(code - codes[0] for code in codes)}
}};
static const lv_font_fmt_txt_cmap_t {name}_cmap[] = {{{{
    .range_start={codes[0]}, .range_length={codes[-1] - codes[0] + 1}, .glyph_id_start=1,
    .unicode_list={name}_unicode, .list_length={len(codes)}, .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
}}}};
static const lv_font_fmt_txt_dsc_t {name}_data = {{
    .glyph_bitmap={name}_bitmap, .glyph_dsc={name}_glyphs, .cmaps={name}_cmap,
    .cmap_num=1, .bpp=4, .bitmap_format=LV_FONT_FMT_TXT_PLAIN
}};
static const lv_font_t {name} = {{
    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,
    .line_height={data['lineHeight']}, .base_line={data['baseline']},
    .underline_position=-1, .underline_thickness=1, .dsc=&{name}_data,
    .fallback=&lv_font_montserrat_14
}};
"""


def builtin_font(index):
    """Read the pinned LVGL's actual glyphs for pixel-accurate browser text previews."""
    path = ROOT / "managed_components/lvgl__lvgl/src/font" / (FONT_SYMBOLS[index] + ".c")
    if not path.exists():
        raise schema.DesignError("Build the root app once to download the pinned LVGL fonts.")
    source = re.sub(r"/\*.*?\*/", "", path.read_text(), flags=re.S)

    def body(name):
        match = re.search(r"\b" + re.escape(name) + r"\[\]\s*=\s*\{(.*?)\};", source, re.S)
        if not match:
            raise schema.DesignError("The pinned LVGL font format was not recognized.")
        return match[1]

    def array_values(name):
        return [int(v, 0) for v in re.findall(r"-?0x[0-9a-fA-F]+|-?\d+", body(name))]

    def fields(value):
        return dict(re.findall(r"\.(\w+)\s*=\s*([^,}\s]+)", value))

    descriptors = [fields(s) for s in re.findall(r"\{([^{}]*)\}", body("glyph_dsc"))]
    glyphs = {}
    for raw in re.findall(r"\{([^{}]*)\}", body("cmaps")):
        cmap = fields(raw)
        start, length, first = (int(cmap[k], 0) for k in ("range_start", "range_length", "glyph_id_start"))
        kind = cmap["type"]
        if "SPARSE" in kind:
            offsets = array_values(cmap["unicode_list"])
        else:
            offsets = list(range(length))
        glyph_offsets = array_values(cmap["glyph_id_ofs_list"]) if "FULL" in kind else None
        for i, offset in enumerate(offsets):
            glyph_id = first + (glyph_offsets[i] if glyph_offsets is not None else i)
            d = descriptors[glyph_id]
            glyphs[start + offset] = dict(index=int(d["bitmap_index"], 0), advance=int(d["adv_w"], 0),
                                         width=int(d["box_w"], 0), height=int(d["box_h"], 0),
                                         ox=int(d["ofs_x"], 0), oy=int(d["ofs_y"], 0))
    return dict(bitmap=bytes(array_values("glyph_bitmap")), glyphs=glyphs,
                bpp=int(re.search(r"\.bpp\s*=\s*(\d+)", source)[1]),
                lineHeight=int(re.search(r"\.line_height\s*=\s*(\d+)", source)[1]),
                baseline=int(re.search(r"\.base_line\s*=\s*(\d+)", source)[1]))


def font_preview(data):
    Image, _, _ = pillow()
    positions, x, y, row = {}, 0, 0, 0
    for code, glyph in data["glyphs"].items():
        w, h = glyph["width"], glyph["height"]
        if x + w + 1 > 512:
            x, y, row = 0, y + row + 1, 0
        positions[code] = dict(glyph, x=x, y=y)
        x += w + 1
        row = max(row, h)
    atlas = Image.new("RGBA", (512, max(1, y + row)))
    bpp = data["bpp"]
    for glyph in positions.values():
        for pixel in range(glyph["width"] * glyph["height"]):
            bit = pixel * bpp
            byte = data["bitmap"][glyph["index"] + bit // 8]
            alpha = ((byte >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1)) * 255 // ((1 << bpp) - 1)
            atlas.putpixel((glyph["x"] + pixel % glyph["width"],
                            glyph["y"] + pixel // glyph["width"]), (255, 255, 255, alpha))
    encoded = io.BytesIO()
    atlas.save(encoded, format="PNG")
    return dict(lineHeight=data["lineHeight"], baseline=data["baseline"], glyphs=positions,
                data="data:image/png;base64," + base64.b64encode(encoded.getvalue()).decode())


def compile_project(source):
    project = schema.normalize(source)
    lines = ["/* Generated by tools/designer/compiler.py. Edit design/project.json or use ./designer. */",
             "#ifndef GENERATED_BEEPER_DESIGN_H", "#define GENERATED_BEEPER_DESIGN_H",
             f"static const char *const design_name = {c_string(project['name'])};"]
    assets, storage, font_ids, font_symbols = [], {}, list(schema.FONTS), list(FONT_SYMBOLS)
    font_size = 0
    for asset in project["assets"]:
        if asset["kind"] == "font":
            data = custom_font(asset, project)
            font_size += len(data["bitmap"])
            if font_size > schema.MAX_FONT_BYTES:
                raise schema.DesignError("Imported glyph bitmaps exceed 500 KB. Reduce font size or extra glyphs.")
            name = f"design_font_{len(font_symbols)}"
            lines.append(font_c(data, name))
            font_ids.append(asset["id"])
            font_symbols.append(name)
        else:
            frames, durations = decode_image(asset)
            binary = b"".join(frame_bytes(frame) for frame in frames)
            filename = "a" + hashlib.sha256(binary).hexdigest()[:16] + ".bin"
            storage[filename] = binary
            if sum(map(len, storage.values())) > schema.MAX_STORAGE_BYTES:
                raise schema.DesignError("Combined decoded images/GIFs exceed the 6 MB FAT asset budget.")
            index = len(assets)
            lines.append(f"static const uint32_t asset_{index}_durations[] = {{{','.join(map(str, durations))}}};")
            assets.append(dict(id=asset["id"], name=filename, width=asset["width"], height=asset["height"],
                               count=len(frames), frameBytes=len(binary) // len(frames), durations=durations))
    lines.append("static const lv_font_t *const design_fonts[] = {" +
                 ",".join("&" + name for name in font_symbols) + "};")
    lines.append("static const design_asset_t design_assets[] = {")
    for i, asset in enumerate(assets):
        lines.append("    {%s,%d,%d,%d,%d,asset_%d_durations}," %
                     (c_string("/spiflash/design/" + asset["name"]), asset["width"], asset["height"],
                      asset["count"], asset["frameBytes"], i))
    if not assets:
        lines.append("    {0},")
    lines += ["};", f"static const size_t design_asset_count = {len(assets)};"]
    for i, layer in enumerate(project["layers"]):
        keys = layer["animation"]["keys"]
        if keys:
            lines.append(f"static const design_key_t layer_{i}_keys[] = {{")
            lines.extend("    {%d,%d,%d,%d}," % (k["at"], k["x"], k["y"], k["opacity"]) for k in keys)
            lines.append("};")
    lines.append("static const design_layer_t design_layers[] = {")
    for i, layer in enumerate(project["layers"]):
        animation = layer["animation"]
        index = next((i for i, a in enumerate(assets) if a["id"] == layer["asset"]), -1)
        lines.append(
            "    {.kind=%d,.scene=%d,.binding=%d,.x=%d,.y=%d,.width=%d,.height=%d,"
            ".color=%s,.opacity=%d,.font=%d,.align=%d,.scroll=%s,.asset=%d,.text=%s,"
            ".period_ms=%d,.keys=%s,.key_count=%d}," %
            (schema.KINDS.index(layer["kind"]), schema.SCENES.index(layer["scene"]),
             schema.BINDINGS.index(layer["binding"]), layer["x"], layer["y"], layer["width"], layer["height"],
             c_color(layer["color"]), layer["opacity"], font_ids.index(layer["font"]),
             ("left", "center", "right").index(layer["align"]), c_bool(layer["scroll"]), index,
             c_string(layer["text"]), animation["period"],
             f"layer_{i}_keys" if animation["keys"] else "NULL", len(animation["keys"])))
    lines += ["};", "static const size_t design_layer_count = sizeof(design_layers)/sizeof(design_layers[0]);",
              "static const uint32_t design_backgrounds[] = {" +
              ",".join(c_color(project["scenes"][name]["background"]) for name in schema.SCENES) + "};"]
    for name in ("idle", "message", "alarm"):
        lines.append(f"static const design_step_t effect_{name}[] = {{")
        for step in project["effects"][name]["steps"]:
            lines.append("    {%d,%s,%s,%d,%d,%d,%d,%d,%s,%s}," %
                         (step["duration"], c_color(step["color"]), c_color(step["screenColor"]),
                          step["frequency"], step["brightness"], step["vibration"], step["volume"],
                          schema.WAVES.index(step["wave"]), c_bool(step["blend"]), c_bool(step["screen"])))
        lines.append("};")
    lines.append("static const design_sequence_t design_sequences[] = {")
    for name in ("idle", "message", "alarm"):
        effect = project["effects"][name]
        lines.append(f"    {{effect_{name},{len(effect['steps'])},{c_bool(effect['loop'])}}},")
    h = project["hardware"]
    lines += ["};", "static const design_hardware_t design_hardware = { .buttons = {"]
    buttons = h["buttons"] + [dict(enabled=False, gpio=2, action="acknowledge", activeLow=True)] * (4 - len(h["buttons"]))
    for b in buttons:
        lines.append("    {%d,%d,%s}," % (b["gpio"] if b["enabled"] else -1,
                                         schema.ACTIONS.index(b["action"]), c_bool(b["activeLow"])))
    audio = h["audio"]
    lines += ["}, .motor_gpio=%d,.speaker=%s,.microphone=%s,.bclk=%d,.ws=%d,.dout=%d,.din=%d};" %
              (h["motor"]["gpio"] if h["motor"]["enabled"] else -1,
               c_bool(audio["speaker"]), c_bool(audio["microphone"]),
               audio["bclk"], audio["ws"], audio["dout"], audio["din"]), "#endif\n"]
    return project, "\n".join(lines), storage, dict(
        layers=len(project["layers"]), images=len(assets), fonts=len(font_symbols),
        storageBytes=sum(map(len, storage.values())), fontBytes=font_size, assets=assets)


def atomic_write(path, data):
    path = Path(path)
    if isinstance(data, str):
        data = data.encode()
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return
    fd, temporary = tempfile.mkstemp(prefix="." + path.name, dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def write_compiled(source, output, storage_path):
    project, header, assets, report = compile_project(source)
    storage_path = Path(storage_path)
    storage_path.mkdir(parents=True, exist_ok=True)
    for name, data in assets.items():
        atomic_write(storage_path / name, data)
    for stale in storage_path.glob("a*.bin"):
        if stale.name not in assets:
            stale.unlink()
    atomic_write(Path(output) / "design_data.h", header)
    atomic_write(Path(output) / "design-report.json", json.dumps(report, indent=2) + "\n")
    return project, report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--storage", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.project.stat().st_size > schema.MAX_PROJECT_BYTES:
            raise schema.DesignError("Project exceeds 16 MB.")
        _, report = write_compiled(json.loads(args.project.read_text()), args.out, args.storage)
        print(f"Design: {report['layers']} layers, {report['images']} images, "
              f"{report['storageBytes']} storage bytes, {report['fontBytes']} imported-font bytes")
    except (schema.DesignError, OSError, ValueError) as error:
        parser.exit(1, f"Design compilation failed: {error}\n")


if __name__ == "__main__":
    main()
