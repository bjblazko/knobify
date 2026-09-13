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
- USB mass-storage ("drive") mode for copying music without swapping the
  SD card — the ESP32-S3's native USB-OTG could expose the SD card as a
  USB drive via TinyUSB's MSC class while plugged in, so the card
  wouldn't need to be physically removed and read on another computer

These are acknowledged future ideas, not requirements yet — don't design around them prematurely.

## Status

Hardware identified and documented. Process framework (ADRs, arc42,
coding guidelines, testing strategy) and toolchain skeleton (PlatformIO +
Arduino, native + esp32-s3 environments) are in place — see
[`docs/adr/`](docs/adr/README.md) and [`docs/arc42/arc42.md`](docs/arc42/arc42.md)
for what was decided and why.

v1 UX is designed and implemented end-to-end (navigation, library
indexing, playback, input routing, display/touch/LVGL screens) but not
yet verified on real hardware — see
[ADR 0004](docs/adr/0004-navigation-library-and-index-architecture.md)
for what's built and what's still unverified. v1 scope: play MP3/WAV/OGG
from an SD card, browse a library by artist/album (or raw folders),
control via touch + one rotary encoder, fully offline — see
[ADR 0002](docs/adr/0002-v1-format-and-mcu-scope.md).

## Starting a new session here

The next step is UX/design work for v1 (navigation model, screen
structure, library/metadata handling on the SD card), followed by actual
feature implementation on top of the existing scaffold. A good first
prompt for that session:

> Read README.md, device.md, docs/adr/, and docs/arc42/arc42.md. Let's design the UX for v1 (navigation model, screen structure for browsing + now-playing, how the rotary encoder and touch interact) and then start implementing it inside the existing PlatformIO scaffold, following docs/coding-guidelines.md.
