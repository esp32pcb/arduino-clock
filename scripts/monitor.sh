#!/usr/bin/env bash
# Serial monitor. Usage: scripts/monitor.sh [PORT] [SECONDS]
#
# Not arduino-cli monitor: that one asserts DTR/RTS on open, which on these
# auto-reset boards holds the chip in reset -- the log stays empty and it looks
# like the firmware is dead. Open the port with both lines deasserted instead.
set -euo pipefail
PORT="${1:-/dev/ttyUSB0}"
SECONDS_LIMIT="${2:-0}"   # 0 = run until Ctrl-C

exec python3 - "$PORT" "$SECONDS_LIMIT" <<'EOF'
import sys, time, serial

port, limit = sys.argv[1], float(sys.argv[2])
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 1
s.dtr = s.rts = False
s.open()
s.dtr = s.rts = False          # again: pyserial re-asserts on open

end = time.time() + limit if limit > 0 else float("inf")
try:
    while time.time() < end:
        data = s.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
except (KeyboardInterrupt, BrokenPipeError):
    pass                       # Ctrl-C, or piped into head/grep that exited
finally:
    s.close()
EOF
