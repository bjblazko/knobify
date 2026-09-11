# 0002: v1 format and MCU scope

## Status

Accepted — 2026-09-11

## Context

The developer's actual music library (on a home NAS, to be copied to SD)
includes M4A (AAC/ALAC) files. The developer wants to publish `knobify` as
open source from Germany and is concerned about licensing complications
from bundling an AAC decoder.

`device.md` documents this board as dual-MCU: an ESP32-S3R8 primary and an
ESP32-U4WDH secondary, each with its own rotary encoder. Third-party
research on this board (CNX-Software writeup, referenced in `device.md`)
indicates the secondary MCU exists mainly to add Classic Bluetooth support
— the ESP32-S3 doesn't support Classic Bluetooth, only BLE — which is
already explicitly out of scope for `knobify` per `README.md`.

## Decision

- **v1 supported formats**: MP3, WAV, OGG/Vorbis. AAC/M4A is explicitly
  **not** supported natively, to avoid AAC licensing exposure in an
  open-source project. M4A files in the user's library will instead be
  converted to a supported format by a separate, later conversion
  script/tool — not part of the firmware.
- **v1 targets the primary ESP32-S3R8 MCU only.** Display, touch, one
  rotary encoder, audio output, SD card, and all application logic run on
  the primary MCU. The secondary ESP32-U4WDH and its rotary encoder are
  out of scope for v1 and are not programmed at all in this iteration.

## Consequences

- No AAC/M4A licensing risk in the shipped firmware.
- The developer needs a conversion step before copying their existing
  library to the SD card — acceptable one-time friction, tracked as
  future work rather than blocking v1.
- v1 firmware only uses one of the board's two rotary encoders. Dual-
  encoder support and any inter-MCU communication protocol is deferred; if
  it's later wanted, it requires new design work (and a new ADR) since
  today's research didn't establish how the two MCUs are expected to talk
  to each other for non-Bluetooth purposes.
