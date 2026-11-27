"""Bounded, versioned design format shared by the editor and firmware compiler."""
from __future__ import annotations

import base64
import copy
import re

VERSION = 1
WIDTH, HEIGHT = 172, 320
MAX_PROJECT_BYTES = 16 * 1024 * 1024
MAX_STORAGE_BYTES = 6 * 1024 * 1024
MAX_FONT_BYTES = 500 * 1024
GPIO_PINS = (2, 4, 5, 6, 7, 8, 9, 10, 11)
SCENES = ("home", "message", "alarm")
BINDINGS = ("static", "clock", "date", "alarm", "message", "reply", "hint", "network", "microphone")
KINDS = ("text", "box", "ellipse", "image")
FONTS = ("montserrat14", "montserrat20", "montserrat28", "montserrat32", "mono8", "mono16")
ACTIONS = ("nextReply", "sendReply", "acknowledge", "editAlarm")
WAVES = ("sine", "square", "triangle")


class DesignError(ValueError):
    pass


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise DesignError(f"{label} must be an integer between {low} and {high}.")
    return value


def boolean(value, label):
    if type(value) is not bool:
        raise DesignError(f"{label} must be true or false.")
    return value


def text(value, limit, label):
    if not isinstance(value, str) or len(value) > limit:
        raise DesignError(f"{label} must be text of at most {limit} characters.")
    if any((ord(c) < 32 and c not in "\n\t") or 0xD800 <= ord(c) <= 0xDFFF for c in value):
        raise DesignError(f"{label} contains unsupported control characters.")
    return value


def color(value, label):
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        raise DesignError(f"{label} must be a six-digit hex color.")
    return value.lower()


def choice(value, values, label):
    if value not in values:
        raise DesignError(f"{label} must be one of: {', '.join(values)}.")
    return value


def object_value(value, label):
    if not isinstance(value, dict):
        raise DesignError(f"{label} must be an object.")
    return value


def array(value, limit, label):
    if not isinstance(value, list) or len(value) > limit:
        raise DesignError(f"{label} must be an array of at most {limit} items.")
    return value


def asset_bytes(asset):
    data = asset.get("data", "")
    if not isinstance(data, str) or not data.startswith("data:") or ";base64," not in data:
        raise DesignError(f"Asset {asset.get('name', '')} needs a base64 data URL.")
    try:
        decoded = base64.b64decode(data.split(";base64,", 1)[1], validate=True)
    except (ValueError, base64.binascii.Error):
        raise DesignError("Asset data is not valid base64.") from None
    if not decoded or len(decoded) > 2 * 1024 * 1024:
        raise DesignError("Each source image/GIF/font must be between 1 byte and 2 MB.")
    return decoded


def step(duration=500, led="#0000ff", **changes):
    result = dict(duration=duration, color=led, brightness=100, vibration=0,
                  frequency=0, volume=0, wave="sine", blend=False,
                  screen=False, screenColor="#101820")
    result.update(changes)
    return result


def default_project():
    home = [
        ("title", "static", 8, 6, 156, 20, "montserrat14", "#80bfff", "CHILD BEEPER"),
        ("clock", "clock", 8, 29, 156, 40, "montserrat32", "#ffffff", ""),
        ("date", "date", 8, 71, 156, 20, "montserrat14", "#abc0d0", ""),
        ("alarm", "alarm", 8, 92, 156, 20, "montserrat14", "#ffdb61", ""),
        ("message", "message", 8, 117, 156, 80, "montserrat14", "#ffffff", ""),
        ("reply", "reply", 8, 203, 156, 20, "montserrat14", "#80bfff", ""),
        ("hint", "hint", 8, 226, 156, 51, "montserrat14", "#abc0d0", ""),
        ("network", "network", 8, 282, 156, 38, "montserrat14", "#80bfff", ""),
    ]
    layers = []
    for scene in SCENES:
        for ident, binding, x, y, w, h, font, foreground, label in home:
            if ident == "title":
                label = {"home": "CHILD BEEPER", "message": "NEW MESSAGE", "alarm": "WAKE UP!"}[scene]
            layers.append(dict(id=f"{scene}_{ident}", name=ident.title(), scene=scene,
                               kind="text", binding=binding, x=x, y=y, width=w, height=h,
                               font=font, color=foreground, opacity=255, align="left",
                               scroll=binding == "message", text=label, asset="",
                               animation=dict(period=2000, keys=[])))
    return dict(
        version=VERSION, name="Child Beeper",
        scenes={name: {"background": "#101820"} for name in SCENES},
        layers=layers, assets=[],
        hardware=dict(
            buttons=[dict(enabled=False, gpio=pin, activeLow=True, action=ACTIONS[i])
                     for i, pin in enumerate((2, 4, 5, 6))],
            motor=dict(enabled=False, gpio=7),
            audio=dict(speaker=False, microphone=False, bclk=8, ws=9, dout=10, din=11)),
        effects=dict(
            idle=dict(loop=True, steps=[step(1000)]),
            message=dict(loop=True, steps=[
                step(120, blend=True), step(140, "#ffff00", blend=True),
                step(100, blend=True), step(190, "#ffff00", blend=True), step(850)]),
            alarm=dict(loop=True, steps=[
                step(300, "#ffff00", vibration=60, frequency=880, volume=20,
                     screen=True, screenColor="#704400"),
                step(300, "#0000ff", frequency=660, volume=12,
                     screen=True, screenColor="#101820")]),
        ),
    )


def normalize(source):
    source = object_value(source, "Project")
    if source.get("version") != VERSION or type(source.get("version")) is not int:
        raise DesignError(f"Unsupported design version; expected {VERSION}.")
    defaults = default_project()
    result = dict(version=VERSION, name=text(source.get("name", defaults["name"]), 80, "Project name"))
    scenes = object_value(source.get("scenes", defaults["scenes"]), "Scenes")
    result["scenes"] = {}
    for scene in SCENES:
        value = object_value(scenes.get(scene), scene)
        result["scenes"][scene] = dict(background=color(value.get("background"), scene + " background"))
    assets = []
    ids = set()
    source_bytes = 0
    for raw in array(source.get("assets", []), 8, "Assets"):
        raw = object_value(raw, "Asset")
        ident = text(raw.get("id"), 48, "Asset ID")
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*", ident) or ident in ids:
            raise DesignError("Asset IDs must be unique letters/numbers, underscores or hyphens.")
        ids.add(ident)
        kind = choice(raw.get("kind"), ("image", "font"), "Asset type")
        value = dict(id=ident, name=text(raw.get("name", ident), 120, "Asset name"), kind=kind,
                     data=raw.get("data"))
        source_bytes += len(asset_bytes(value))
        if kind == "image":
            value["width"] = integer(raw.get("width", 96), 1, WIDTH, "Image width")
            value["height"] = integer(raw.get("height", 96), 1, HEIGHT, "Image height")
        else:
            value["size"] = integer(raw.get("size", 20), 8, 48, "Imported font size")
            value["characters"] = text(raw.get("characters", ""), 256, "Extra font characters")
        assets.append(value)
    if source_bytes > 8 * 1024 * 1024:
        raise DesignError("Combined source assets exceed 8 MB.")
    fonts = [a["id"] for a in assets if a["kind"] == "font"]
    images = [a["id"] for a in assets if a["kind"] == "image"]
    if len(fonts) > 4:
        raise DesignError("At most four imported font faces/sizes are supported per project.")
    result["assets"] = assets
    layers, ids = [], set()
    for raw in array(source.get("layers", defaults["layers"]), 48, "Layers"):
        raw = object_value(raw, "Layer")
        ident = text(raw.get("id"), 48, "Layer ID")
        if not ident or ident in ids:
            raise DesignError("Layer IDs must be unique and nonempty.")
        ids.add(ident)
        kind = choice(raw.get("kind", "text"), KINDS, "Layer type")
        layer = dict(
            id=ident, name=text(raw.get("name", ident), 80, "Layer name"),
            scene=choice(raw.get("scene", "home"), SCENES, "Scene"), kind=kind,
            binding=choice(raw.get("binding", "static"), BINDINGS, "Text binding"),
            x=integer(raw.get("x", 8), -WIDTH, WIDTH, "X position"),
            y=integer(raw.get("y", 8), -HEIGHT, HEIGHT, "Y position"),
            width=integer(raw.get("width", 120), 1, WIDTH, "Width"),
            height=integer(raw.get("height", 24), 1, HEIGHT, "Height"),
            color=color(raw.get("color", "#ffffff"), "Layer color"),
            opacity=integer(raw.get("opacity", 255), 0, 255, "Opacity"),
            font=choice(raw.get("font", FONTS[0]), (*FONTS, *fonts), "Font"),
            align=choice(raw.get("align", "left"), ("left", "center", "right"), "Alignment"),
            scroll=boolean(raw.get("scroll", False), "Scroll text"),
            text=text(raw.get("text", ""), 512, "Layer text"), asset=raw.get("asset", ""),
        )
        if kind == "image" and layer["asset"] not in images:
            raise DesignError(f"Image layer {layer['name']} needs an imported image/GIF.")
        if kind != "image":
            layer["asset"] = ""
        animation = object_value(raw.get("animation", {"period": 2000, "keys": []}), "Animation")
        period = integer(animation.get("period", 2000), 100, 60000, "Animation duration")
        keys = []
        previous = -1
        for key in array(animation.get("keys", []), 32, "Keyframes"):
            key = object_value(key, "Keyframe")
            at = integer(key.get("at"), 0, period, "Keyframe time")
            if at <= previous:
                raise DesignError("Keyframe times must be strictly increasing.")
            previous = at
            keys.append(dict(at=at, x=integer(key.get("x"), -WIDTH, WIDTH, "Keyframe X"),
                             y=integer(key.get("y"), -HEIGHT, HEIGHT, "Keyframe Y"),
                             opacity=integer(key.get("opacity", 255), 0, 255, "Keyframe opacity")))
        if keys and keys[0]["at"] != 0:
            raise DesignError("An animation's first keyframe must be at 0 ms.")
        layer["animation"] = dict(period=period, keys=keys)
        layers.append(layer)
    if any(not any(layer["scene"] == s for layer in layers) for s in SCENES):
        raise DesignError("Each scene needs at least one layer.")
    result["layers"] = layers
    effects = object_value(source.get("effects", defaults["effects"]), "Effects")
    result["effects"] = {}
    for trigger in ("idle", "message", "alarm"):
        effect = object_value(effects.get(trigger), trigger + " effect")
        steps = []
        for raw in array(effect.get("steps"), 32, "Effect steps"):
            raw = object_value(raw, "Effect step")
            value = step()
            value.update(raw)
            steps.append(dict(
                duration=integer(value["duration"], 20, 5000, "Step duration"),
                color=color(value["color"], "LED color"),
                brightness=integer(value["brightness"], 0, 100, "LED brightness"),
                vibration=integer(value["vibration"], 0, 100, "Vibration strength"),
                frequency=integer(value["frequency"], 0, 6000, "Tone frequency"),
                volume=integer(value["volume"], 0, 100, "Tone volume"),
                wave=choice(value["wave"], WAVES, "Waveform"),
                blend=boolean(value["blend"], "Smooth transition"),
                screen=boolean(value["screen"], "Flash screen"),
                screenColor=color(value["screenColor"], "Screen color"),
            ))
        if not steps or sum(s["duration"] for s in steps) > 60000:
            raise DesignError("Each effect needs 1–32 steps totaling at most 60 seconds.")
        result["effects"][trigger] = dict(loop=boolean(effect.get("loop", True), "Loop effect"), steps=steps)
    hardware = copy.deepcopy(object_value(source.get("hardware", defaults["hardware"]), "Hardware"))
    claimed = {}

    def pin(value, role, enabled):
        value = integer(value, 0, 48, role + " GPIO")
        if value not in GPIO_PINS:
            raise DesignError(f"{role}: GPIO {value} is reserved or not an available header pin.")
        if enabled:
            if value in claimed:
                raise DesignError(f"GPIO {value} is already assigned to {claimed[value]}.")
            claimed[value] = role
        return value

    buttons = []
    for i, raw in enumerate(array(hardware.get("buttons", []), 4, "External buttons")):
        raw = object_value(raw, "Button")
        enabled = boolean(raw.get("enabled", False), "Button enabled")
        buttons.append(dict(enabled=enabled, gpio=pin(raw.get("gpio"), f"Button {i + 1}", enabled),
                            activeLow=boolean(raw.get("activeLow", True), "Button active low"),
                            action=choice(raw.get("action", ACTIONS[i]), ACTIONS, "Button action")))
    motor = object_value(hardware.get("motor", defaults["hardware"]["motor"]), "Motor")
    enabled = boolean(motor.get("enabled", False), "Motor enabled")
    motor = dict(enabled=enabled, gpio=pin(motor.get("gpio"), "Motor", enabled))
    audio = object_value(hardware.get("audio", defaults["hardware"]["audio"]), "Audio")
    speaker = boolean(audio.get("speaker", False), "Speaker enabled")
    microphone = boolean(audio.get("microphone", False), "Microphone enabled")
    audio = dict(speaker=speaker, microphone=microphone,
                 bclk=pin(audio.get("bclk"), "Audio clock", speaker or microphone),
                 ws=pin(audio.get("ws"), "Audio word-select", speaker or microphone),
                 dout=pin(audio.get("dout"), "Speaker data", speaker),
                 din=pin(audio.get("din"), "Microphone data", microphone))
    result["hardware"] = dict(buttons=buttons, motor=motor, audio=audio)
    return result
