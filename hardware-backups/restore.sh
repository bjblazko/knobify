#!/usr/bin/env bash
# Writes a factory backup from this folder back onto the board.
#
# Which MCU you reach depends on which way round the USB-C cable is
# plugged in (see README.md) — this script does not try to guess that
# for you, it just lists the connected serial ports so you can confirm.
#
# Usage:
#   restore.sh primary   # ESP32-S3, esp32s3_primary_mcu_16MB_factory.bin
#   restore.sh secondary # ESP32-U4WDH, u4wdh_secondary_mcu_4MB_factory.bin
set -euo pipefail
cd "$(dirname "$0")"

ESPTOOL_PY="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
ESPTOOL_PYTHON="$(command -v python3 || true)"
# The system Python often lacks pyserial; PlatformIO's own Python (which
# esptool was installed under) reliably has it.
PIO_PYTHON="$(find /opt/homebrew/Cellar/platformio -maxdepth 3 -path '*/libexec/bin/python' 2>/dev/null | head -1)"
if [[ -n "$PIO_PYTHON" ]]; then
  ESPTOOL_PYTHON="$PIO_PYTHON"
fi

if [[ ! -f "$ESPTOOL_PY" ]]; then
  echo "esptool.py not found at $ESPTOOL_PY — run 'pio run' once in the project root to install it." >&2
  exit 1
fi

target="${1:-}"
case "$target" in
  primary)
    file="esp32s3_primary_mcu_16MB_factory.bin"
    expect_chip="ESP32-S3"
    ;;
  secondary)
    file="u4wdh_secondary_mcu_4MB_factory.bin"
    expect_chip="ESP32-U4WDH / ESP32"
    ;;
  *)
    echo "Usage: $0 primary|secondary" >&2
    echo "  primary   -> restores esp32s3_primary_mcu_16MB_factory.bin (ESP32-S3, native USB, /dev/cu.usbmodem*)" >&2
    echo "  secondary -> restores u4wdh_secondary_mcu_4MB_factory.bin (ESP32-U4WDH, CH340 bridge, /dev/cu.usbserial-*)" >&2
    exit 1
    ;;
esac

if [[ ! -f "$file" ]]; then
  echo "Backup file not found: $file" >&2
  exit 1
fi

echo "Available serial ports:"
ls /dev/cu.* 2>/dev/null || true
echo
echo "Target: $target ($expect_chip), file: $file"
read -r -p "Serial port to write to (e.g. /dev/cu.usbmodem1201): " port

if [[ -z "$port" || ! -e "$port" ]]; then
  echo "Not a valid port: '$port'" >&2
  exit 1
fi

echo
echo "About to write $file to $port. This overwrites everything currently on that chip's flash."
read -r -p "Type 'yes' to continue: " confirm
if [[ "$confirm" != "yes" ]]; then
  echo "Aborted."
  exit 1
fi

"$ESPTOOL_PYTHON" "$ESPTOOL_PY" --port "$port" write_flash 0x0 "$file"
