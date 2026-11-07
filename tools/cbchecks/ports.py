"""Serial port discovery and the board guard (exit code 2 with recovery hints)."""

from __future__ import annotations

import glob
import os
import stat
from pathlib import Path

from .errors import BoardError

PORT_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")

FORCE_NO_PORTS_ENV = "CB_FORCE_NO_PORTS"

HINT = (
    "cb: no usable serial port found (looked for /dev/ttyACM*, /dev/ttyUSB*).\n"
    "cb: the board is not connected, or the cable is charge-only.\n"
    "  1. connect the board with a USB-C data cable\n"
    "  2. if it does not enumerate: hold BOOT, tap RESET, release BOOT (download mode)\n"
    "  3. check `dmesg | tail` and that your user is in the `dialout` group\n"
    "  4. then: ./cb ports"
)


def forced_no_ports() -> bool:
    return os.environ.get(FORCE_NO_PORTS_ENV, "").strip() in ("1", "true", "yes", "on")


def list_ports() -> list[str]:
    if forced_no_ports():
        return []
    found: list[str] = []
    for pattern in PORT_GLOBS:
        found.extend(sorted(glob.glob(pattern)))
    # stable, de-duplicated
    return sorted(dict.fromkeys(found))


def is_char_device(device: str) -> bool:
    try:
        return stat.S_ISCHR(os.stat(device).st_mode)
    except OSError:
        return False


def guard_port(explicit: str | None = None) -> str:
    """Return the port to use or raise BoardError (exit code 2)."""
    if explicit:
        if not os.path.exists(explicit):
            raise BoardError(f"cb: requested port {explicit} does not exist.\n" + HINT)
        if not is_char_device(explicit):
            raise BoardError(f"cb: {explicit} is not a character device; refusing to use it.")
        return explicit

    ports = list_ports()
    if not ports:
        raise BoardError(HINT)
    if len(ports) > 1:
        listing = "\n".join(f"  - {p}" for p in ports)
        raise BoardError(
            "cb: more than one serial port is present; refusing to guess.\n"
            f"{listing}\n"
            "cb: pass --port <device> (e.g. ./cb flash --port /dev/ttyACM0)."
        )
    port = ports[0]
    if not is_char_device(port):
        raise BoardError(f"cb: {port} is not a character device; refusing to use it.")
    return port
