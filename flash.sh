#!/usr/bin/env bash
# ./flash.sh — one command to build the child-beeper root app and flash it
# (plus its Wi-Fi settings image) to the connected ESP32-S3 board.
#
#   ./flash.sh                build + flash (./cb flash --target app)
#   ./flash.sh --monitor      also open the serial console afterwards (Ctrl-] quits)
#   ./flash.sh --port /dev/ttyACM0    explicit port (never required)
#
# Wi-Fi credentials: wifi_settings.txt in this directory (created from
# wifi_settings.example.txt on first run). Edit it and re-run this script.
# Without a board (or with a charge-only cable) the script exits 2 and prints
# the BOOT+RESET recovery hints.
set -euo pipefail

root="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"

# First run: create the settings file from the committed template.
if [[ ! -f wifi_settings.txt ]]; then
    cp wifi_settings.example.txt wifi_settings.txt
    echo "flash.sh: created wifi_settings.txt from the example."
    echo "flash.sh: edit your SSID/password there, then re-run ./flash.sh"
fi

monitor=0
port_args=()
while (($# > 0)); do
    case "$1" in
        -m|--monitor)
            monitor=1 ;;
        --port)
            if (($# < 2)); then
                echo "flash.sh: --port needs a value (e.g. /dev/ttyACM0)" >&2
                exit 3
            fi
            port_args=(--port "$2")
            shift ;;
        *)
            echo "flash.sh: unknown argument: $1 (use: ./flash.sh [--monitor] [--port DEV])" >&2
            exit 3 ;;
    esac
    shift
done

if grep -Eq '^[[:space:]]*ssid[[:space:]]*=[[:space:]]*(YOUR_SSID)?[[:space:]]*$' wifi_settings.txt; then
    echo "flash.sh: warning: wifi_settings.txt has no real SSID — the device will boot with Wi-Fi off."
fi

echo "flash.sh: building + flashing 'app' …"
./cb flash --target app "${port_args[@]}"

if ((monitor)); then
    echo "flash.sh: opening the serial console — press Ctrl-] to quit"
    ./cb monitor --target app "${port_args[@]}"
fi
