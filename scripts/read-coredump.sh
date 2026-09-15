#!/usr/bin/env bash
# Reads and decodes the core dump the firmware writes to flash when it
# panics (the `coredump` partition, enabled in the Arduino core's prebuilt
# sdkconfig). With TinyUSB the USB serial port comes back only after boot,
# so the panic message itself is never seen -- this shows the crashed task,
# its registers and backtrace instead. Found an M4A crash this way
# (AGENTS.md, 2026-09-16).
#
# Usage: read-coredump.sh [--port /dev/cu.XXXX]
# Leaves the device reset and running. The dump stays in flash until the
# next panic, so rerun only after reproducing the crash. Needs the ELF of
# the firmware that crashed (.pio/build/esp32-s3/firmware.elf); after a
# rebuild esp-coredump refuses the dump (SHA256 mismatch) rather than
# decode it wrongly.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${2:-}"
ELF=.pio/build/esp32-s3/firmware.elf
VENV=.pio/coredump-venv
OUT="$(mktemp -t coredump).bin"
TOOLCHAIN="$HOME/.platformio/packages/toolchain-xtensa-esp32s3/bin"

if [[ ! -x "$VENV/bin/esp-coredump" ]]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q esp-coredump esptool
fi

find_port() {
  pio device list --json-output 2>/dev/null | python3 -c '
import json, sys
want_jtag = sys.argv[1] == "jtag"
for d in json.load(sys.stdin):
    if "303A:1001" in d.get("hwid", "").upper() and ("JTAG" in d.get("description", "")) == want_jtag:
        print(d["port"]); break
' "$1"
}

# A running TinyUSB firmware reboots into the ROM bootloader on a 1200-baud
# open (same as flash-primary-mcu.sh).
[[ -z "$PORT" ]] && PORT="$(find_port app)"
if [[ -n "$PORT" ]]; then
  python3 -c 'import sys, termios, os
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); a[4] = a[5] = termios.B1200
termios.tcsetattr(fd, termios.TCSANOW, a); os.close(fd)' "$PORT" || true
  sleep 3
fi
PORT="$(find_port jtag)"
[[ -n "$PORT" ]] || { echo "No bootloader port found" >&2; exit 1; }

"$VENV/bin/esptool" --chip esp32s3 --port "$PORT" --before no-reset \
  --after hard-reset read-flash 0xff0000 0x10000 "$OUT" >/dev/null
"$VENV/bin/esp-coredump" --chip esp32s3 info_corefile --core "$OUT" \
  --core-format raw --gdb "$TOOLCHAIN/xtensa-esp32s3-elf-gdb" "$ELF" |
  sed -n '/CURRENT THREAD REGISTERS/,/exccause/p;/Crashed task/p;/CURRENT THREAD STACK/,/THREADS INFO/p'
echo "(raw dump: $OUT; windowed return addresses like 0x0203984d are 0x4203984d --"
echo " resolve with $TOOLCHAIN/xtensa-esp32s3-elf-addr2line -pfiaC -e $ELF <addr>)"
