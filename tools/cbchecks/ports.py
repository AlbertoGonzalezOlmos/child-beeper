"""Serial port discovery and the board guard (exit code 2 with recovery hints)."""

from __future__ import annotations

import fnmatch
import glob
import os
import stat

from .errors import BoardError, UsageError

# Use macOS callout devices for discovery, avoiding duplicate cu/tty entries.
PORT_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.usbmodem*", "/dev/cu.usbserial*")

FORCE_NO_PORTS_ENV = "CB_FORCE_NO_PORTS"

HINT = (
    "cb: no usable serial port found (looked for /dev/ttyACM*, /dev/ttyUSB*, "
    "/dev/cu.usbmodem*, /dev/cu.usbserial*).\n"
    "cb: the board is not connected, or the cable is charge-only.\n"
    "  1. connect the board with a USB-C data cable\n"
    "  2. if it does not enumerate: hold BOOT, tap RESET, release BOOT (download mode)\n"
    "  3. Linux: check `dmesg | tail` and the `dialout` group; macOS: check System Information > USB\n"
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


SERIAL_NAME_PATTERNS = ("ttyACM*", "ttyUSB*", "cu.usbmodem*", "cu.usbserial*",
                        "tty.usbmodem*", "tty.usbserial*")


def is_serial_device_name(device: str) -> bool:
    """Accept USB serial names on Linux/macOS, excluding unrelated character devices."""
    return any(fnmatch.fnmatch(os.path.basename(device), pattern) for pattern in SERIAL_NAME_PATTERNS)


def guard_port(explicit: str | None = None) -> str:
    """Return the port to use, or raise BoardError (2) / UsageError (3)."""
    if explicit:
        if not is_serial_device_name(explicit):
            raise UsageError(
                f"--port {explicit} is not a serial board port "
                f"(expected a Linux ttyACM/ttyUSB or macOS cu.usbmodem/cu.usbserial device)."
            )
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
