#!/usr/bin/env bash
# Compile arduino-clock.ino.
#
# Arduino insists the sketch folder be named like the .ino inside it. That holds
# if you cloned this repo under its own name, but not if you renamed the
# directory -- and then the build fails with "main file missing from sketch".
# Staging into build/arduino-clock/ and compiling there works either way.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="esp32:esp32:esp32:CPUFreq=240,FlashFreq=80,UploadSpeed=921600"
STAGE="$ROOT/build/arduino-clock"

"$ROOT/scripts/gen_secrets.sh"

mkdir -p "$STAGE"
cp "$ROOT/arduino-clock.ino" "$STAGE/arduino-clock.ino"
cp "$ROOT/secrets.h" "$STAGE/secrets.h"

arduino-cli compile \
  --fqbn "$FQBN" \
  --warnings all \
  --build-path "$ROOT/build/out" \
  "$STAGE" "$@"
