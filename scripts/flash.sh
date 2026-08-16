#!/usr/bin/env bash
# Build and upload. Usage: scripts/flash.sh [PORT]
#
# Prints the MAC of the board it is about to write to. Worth a glance if you
# have more than one ESP32 plugged in: they are told apart by their USB-serial
# chip, which is often the same part on every board you own.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-/dev/ttyUSB0}"
FQBN="esp32:esp32:esp32:CPUFreq=240,FlashFreq=80,UploadSpeed=921600"

ESPTOOL="$(ls -d "$HOME"/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | tail -1)"
[ -x "$ESPTOOL" ] || { echo "esptool not found" >&2; exit 1; }

mac="$("$ESPTOOL" --port "$PORT" --before default-reset --after no-reset read-mac 2>/dev/null \
       | grep -oE '([0-9a-f]{2}:){5}[0-9a-f]{2}' | head -1)"
[ -n "$mac" ] || { echo "could not read MAC on $PORT" >&2; exit 1; }

echo "==> flashing ${mac} on ${PORT}"

"$ROOT/scripts/build.sh"
arduino-cli upload --fqbn "$FQBN" --port "$PORT" \
  --input-dir "$ROOT/build/out" "$ROOT/build/arduino-clock"
