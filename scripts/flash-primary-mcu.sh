#!/usr/bin/env bash
# Flashes the esp32-s3 firmware to this board's PRIMARY MCU (ESP32-S3R8),
# guarding against the board's most confusing hardware quirk: a single
# CH340 USB-serial port that's switched between the primary ESP32-S3R8
# and secondary ESP32-U4WDH by a CH445P analog switch, where USB-C cable
# orientation affects which MCU actually receives the connection -- see
# device.md "Hardware quirks observed".
#
# esptool already refuses to flash if the connected chip doesn't match
# the target (`FatalError: This chip is ESP32 not ESP32-S3. Wrong --chip
# argument?`), so nothing gets bricked either way -- this script's job is
# just to turn that raw esptool failure into a clear "flip the cable"
# instruction instead of a confusing wall of Python traceback-adjacent
# esptool output.
#
# Usage: flash-primary-mcu.sh [--port /dev/cu.XXXX]
set -euo pipefail
cd "$(dirname "$0")/.."

PORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --port)
      PORT="$2"
      shift 2
      ;;
    *)
      echo "Usage: $0 [--port /dev/cu.XXXX]" >&2
      exit 1
      ;;
  esac
done

# This board's USB-serial chip is a CH340 (idVendor 0x1A86, idProduct
# 0x7523 -- see device.md). Auto-detect it specifically, rather than
# grabbing the first serial port found, since other unrelated
# boards/debug consoles commonly show up too.
if [[ -z "$PORT" ]]; then
  PORT="$(pio device list --json-output 2>/dev/null \
    | python3 -c '
import json, sys
devices = json.load(sys.stdin)
for d in devices:
    hwid = d.get("hwid", "")
    if "VID:PID=1A86:7523" in hwid.upper():
        print(d["port"])
        break
' || true)"

  if [[ -z "$PORT" ]]; then
    echo "Could not auto-detect the board's CH340 serial port." >&2
    echo "Plug it in, or pass one explicitly: $0 --port /dev/cu.XXXX" >&2
    echo "" >&2
    echo "Available serial ports:" >&2
    pio device list >&2
    exit 1
  fi
  echo "Auto-detected board on $PORT"
fi

echo "== Flashing esp32-s3 (primary ESP32-S3R8) via $PORT =="
OUTPUT_FILE="$(mktemp)"
trap 'rm -f "$OUTPUT_FILE"' EXIT

if pio run -e esp32-s3 -t upload --upload-port "$PORT" 2>&1 | tee "$OUTPUT_FILE"; then
  echo "== Flash OK =="
  exit 0
fi

echo ""
if grep -qi "This chip is ESP32 not ESP32-S3" "$OUTPUT_FILE"; then
  echo "!! Talked to the SECONDARY ESP32-U4WDH MCU instead of the primary" >&2
  echo "!! ESP32-S3R8. This board's USB-C cable orientation determines" >&2
  echo "!! which MCU the single CH340 port reaches -- flip the cable and" >&2
  echo "!! re-run this script. See device.md \"Hardware quirks observed\"." >&2
elif grep -qi -E "Failed to connect|A fatal error occurred|Timed out" "$OUTPUT_FILE"; then
  echo "!! Could not talk to any chip at all on $PORT." >&2
  echo "!! Try: flipping the USB-C cable, holding BOOT during connect," >&2
  echo "!! or re-running with a different --port." >&2
else
  echo "!! Upload failed for a reason this script doesn't specifically" >&2
  echo "!! recognize -- see the output above." >&2
fi
exit 1
