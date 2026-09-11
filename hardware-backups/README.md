# Hardware backups

Full factory flash dumps taken before any custom firmware was written to
the board, so the original demo software can be restored at any time.
These `.bin` files are gitignored (see `../.gitignore`) — they're vendor
firmware, not something to publish as part of this open-source project.

Taken 2026-09-11 via `esptool.py read_flash`, using the PlatformIO-bundled
esptool at `~/.platformio/packages/tool-esptoolpy/esptool.py` run with
PlatformIO's own Python (`/opt/homebrew/Cellar/platformio/<version>/libexec/bin/python`,
since the system Python lacked `pyserial`).

## Files

- `esp32s3_primary_mcu_16MB_factory.bin` — primary ESP32-S3R8, full 16MB
  flash. Reachable via the board's **native USB** (shows up as
  `/dev/cu.usbmodem*` on macOS).
- `u4wdh_secondary_mcu_4MB_factory.bin` — secondary ESP32-U4WDH, full 4MB
  flash. Reachable via the board's **CH340 USB-serial** bridge (shows up
  as `/dev/cu.usbserial-*` on macOS).

The board exposes only one of the two MCUs to USB at a time, switched by
the CH445P analog switch. **Which MCU you reach is controlled entirely by
which way round the USB-C cable is plugged in** — unplug it, flip it
180°, plug it back in, and the other chip appears at a different
`/dev/cu.*` port.

## Restoring

Use `restore.sh` — it lists the connected serial ports, asks which one to
write to, and confirms before overwriting anything:

```sh
./restore.sh primary    # ESP32-S3
./restore.sh secondary  # ESP32-U4WDH
```

It uses PlatformIO's bundled `esptool.py` and Python automatically (the
system Python's `esptool` typically lacks `pyserial`), so no extra setup
is needed beyond having run `pio run` at least once in the project root.
