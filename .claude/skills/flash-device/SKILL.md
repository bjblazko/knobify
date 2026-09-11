---
name: flash-device
description: Flash the knobify firmware to the board's primary ESP32-S3R8 MCU, guarding against this board's dual-MCU USB-cable-orientation quirk. Use whenever the user asks to flash, upload, deploy, or test the firmware on the actual hardware.
---

# Flashing knobify to the device

This board (Waveshare ESP32-S3-Knob-Touch-LCD-1.8) has two MCUs sharing
one USB-serial port through a CH445P analog switch. Which MCU actually
receives the connection depends on **which way round the USB-C cable is
plugged in** -- see `device.md`, "Hardware quirks observed". Flashing
`esp32-s3` firmware while the switch is pointed at the secondary
ESP32-U4WDH will fail (harmlessly -- esptool refuses the mismatch rather
than writing anything), but the raw error is a confusing wall of esptool/
Python output if you don't already know why.

`scripts/flash-primary-mcu.sh` wraps `pio run -e esp32-s3 -t upload` and
turns that failure mode into a clear instruction instead.

## How to run it

```bash
./scripts/flash-primary-mcu.sh
```

It auto-detects the board's CH340 serial port (VID:PID 1A86:7523) via
`pio device list`. If auto-detection fails (multiple candidates, or the
port naming is unfamiliar on this OS), pass it explicitly:

```bash
./scripts/flash-primary-mcu.sh --port /dev/cu.usbserial-XXXX
```

## Interpreting the result

- **Exit 0, "== Flash OK =="**: done, nothing more to do.
- **"Talked to the SECONDARY ESP32-U4WDH MCU..."**: tell the user to
  physically flip the USB-C cable around and re-run the script. This is
  the expected, self-correcting failure mode for this board -- not a
  bug, not a wiring problem, just cable orientation.
- **"Could not talk to any chip at all"**: suggest flipping the cable
  anyway (same underlying switch), holding the board's BOOT button while
  it connects, or checking `pio device list` for the right port.
- **Anything else**: read the captured esptool output above the message
  and diagnose normally -- this script only special-cases the two known
  failure modes above, everything else is a real problem to investigate.

Don't loop retrying the same command more than once or twice without
telling the user what's happening and why -- if the script's own
guidance doesn't resolve it, stop and ask them what they're seeing on
the board/cable rather than guessing further.
