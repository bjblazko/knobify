#!/usr/bin/env bash
# Runs scripts/screenshot.py with whichever Python actually has pyserial
# -- PlatformIO always bundles it (for esptool/device monitor), but the
# system `python3` on this machine doesn't. Tries the system one first
# (cheaper, and works if the user has pyserial installed some other way)
# and falls back to searching for PlatformIO's own interpreter.
set -euo pipefail
cd "$(dirname "$0")/.."

if python3 -c "import serial" >/dev/null 2>&1; then
  python3 scripts/screenshot.py "$@"
  exit $?
fi

PIO_PYTHON="$(find -L /opt/homebrew/Cellar/platformio /usr/local/Cellar/platformio \
  -maxdepth 4 -path "*/libexec/bin/python" -type f 2>/dev/null | head -1 || true)"

if [[ -z "$PIO_PYTHON" ]]; then
  echo "Could not find a Python with pyserial installed (checked system" >&2
  echo "python3 and PlatformIO's bundled interpreter). Install pyserial" >&2
  echo "(pip install pyserial) or locate PlatformIO's python manually." >&2
  exit 1
fi

"$PIO_PYTHON" "$(dirname "$0")/screenshot.py" "$@"
exit $?
