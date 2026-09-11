# knobify

Offline music player built on a Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — see [`device.md`](device.md) for full hardware specs (dual MCU, display, audio DAC, encoders, etc.), the official product page, and wiki links.

## Goal

- Play music stored on an SD card, in well-known formats.
- Audio output via the onboard 3.5mm jack (PCM5100A DAC).
- Control via the board's rotary encoder(s) + touch display.
- Fully offline for v1.

## Explicitly out of scope for now

- Bluetooth headphone output
- Wi-Fi-based features (time/date sync, weather, podcasts, internet radio)

These are acknowledged future ideas, not requirements yet — don't design around them prematurely.

## Status

Hardware identified and documented. Process framework (ADRs, arc42,
coding guidelines, testing strategy) and toolchain skeleton (PlatformIO +
Arduino, native + esp32-s3 environments) are in place — see
[`docs/adr/`](docs/adr/README.md) and [`docs/arc42/arc42.md`](docs/arc42/arc42.md)
for what was decided and why.

No player features exist yet (no SD reading, decoding, UI screens, or
encoder handling). v1 scope: play MP3/WAV/OGG from an SD card, browse a
library by artist/album, control via touch + one rotary encoder, fully
offline — see [ADR 0002](docs/adr/0002-v1-format-and-mcu-scope.md).

## Starting a new session here

The next step is UX/design work for v1 (navigation model, screen
structure, library/metadata handling on the SD card), followed by actual
feature implementation on top of the existing scaffold. A good first
prompt for that session:

> Read README.md, device.md, docs/adr/, and docs/arc42/arc42.md. Let's design the UX for v1 (navigation model, screen structure for browsing + now-playing, how the rotary encoder and touch interact) and then start implementing it inside the existing PlatformIO scaffold, following docs/coding-guidelines.md.
