# 0001: Language and framework choice

## Status

Accepted — 2026-09-11

## Context

`knobify` targets a Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (see
[`device.md`](../../device.md)): an ESP32-S3R8 primary MCU with 8MB PSRAM,
a 360×360 IPS touch LCD (ST77916 + CST816), a PCM5100A I2S DAC, and a
rotary encoder.

The developer is experienced with higher-level languages (Python/JS/etc.)
but new to both C/C++ and embedded development. The project should be
sustainable to maintain solo, with logic that's testable without
hardware, and needs a codebase other contributors can pick up (it will be
published open source).

Options considered:

- **Raw ESP-IDF (C/C++)** — the official Espressif SDK. Maximum control
  and closest to vendor reference material, but more boilerplate, no
  first-class host-native unit test story, and reinvents integration of
  audio/display/touch libraries that already exist for Arduino.
- **MicroPython** — much friendlier syntax for someone from higher-level
  languages, fast iteration. But real-time I2S audio playback and display
  driving are performance- and timing-sensitive; MicroPython's ecosystem
  and community support for this specific board and this kind of workload
  is thin and risky.
- **PlatformIO + Arduino framework** — Waveshare's own examples for this
  board use Arduino + ESP-IDF + LVGL. The Arduino ecosystem has mature,
  actively-maintained libraries directly matching our needs: LVGL for UI,
  and the `ESP32-audioI2S` library (schreibfaul1) for MP3/WAV/OGG decode
  and I2S playback. PlatformIO adds project/dependency management and a
  `native` test environment (Unity) for host-side unit tests, which plain
  Arduino IDE doesn't provide.

## Decision

Use **PlatformIO with the Arduino framework**, targeting the primary
ESP32-S3R8 MCU. Key libraries:

- **LVGL** for the touch UI.
- **ESP32-audioI2S** (schreibfaul1) for audio decode + I2S playback.
- PlatformIO's `native` environment (Unity) for host-side unit tests of
  hardware-decoupled logic.

## Consequences

- Fastest path to a working v1 given the developer's background, backed
  by libraries that already solve the hard, timing-sensitive parts
  (decode, I2S, display/touch drivers).
- Less low-level control than raw ESP-IDF; if a future requirement needs
  it (e.g. very tight RAM/CPU budgets), this decision may need revisiting
  via a new ADR.
- Hardware-touching code must be kept behind thin interfaces (see
  [0003](0003-testing-strategy.md)) so business logic stays host-testable
  despite Arduino's tendency to tangle hardware and logic together.
