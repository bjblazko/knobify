# knobify — Architecture Documentation (arc42)

Following the [arc42](https://arc42.org) template. Sections that don't
make sense to fill yet (because the thing they describe doesn't exist as
code) are stubbed with a note on when they'll be completed — they are not
skipped silently, so the document's own state is honest about what's
designed vs. undesigned.

Don't be shy about Mermaid diagrams (component, sequence, state) wherever
a picture would answer the reader's question faster than prose — Context
and Scope, Building Block View, and Runtime View are the sections most
likely to want one.

## 1. Introduction and Goals

`knobify` is offline music player firmware for a Waveshare
ESP32-S3-Knob-Touch-LCD-1.8 board. It plays music stored on an SD card
through the board's 3.5mm audio jack, controlled via the rotary encoder
and touch display.

**v1 goals** (see [ADR 0002](../adr/0002-v1-format-and-mcu-scope.md)):
play MP3/WAV/OGG files from an SD card, browse a library by artist/album
(not just raw folders), control via touch + one rotary encoder, fully
offline.

**Explicitly out of scope**: Bluetooth output, Wi-Fi-based features,
AAC/M4A native decoding, the secondary MCU.

## 2. Constraints

- **Hardware**: fixed to the Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (see
  [`device.md`](../../device.md)) — ESP32-S3R8 (8MB PSRAM), 360×360 IPS
  touch LCD (ST77916/CST816), PCM5100A I2S DAC, one rotary encoder used in
  v1, microSD card socket.
- **Developer background**: solo developer, new to C/C++ and embedded
  development (see [ADR 0001](../adr/0001-language-and-framework-choice.md)).
- **Licensing**: project will be published open source from Germany; no
  AAC decoding to avoid licensing exposure (ADR 0002).
- **No CI dependency**: not pushed to GitHub in early iterations; local
  verification (`scripts/check.sh`) must work standalone
  (ADR 0003).

## 3. Context and Scope

*To be completed once the SD card library format, and the boundary
between "what's on the SD card" vs. "what the firmware assumes/indexes",
is designed — next session's UX/design work.*

## 4. Solution Strategy

- PlatformIO + Arduino framework, targeting the primary ESP32-S3R8 MCU
  only for v1 (ADR 0001, ADR 0002).
- LVGL for UI, `ESP32-audioI2S` for decode/playback.
- Hardware/logic separation throughout, enabling host-native unit testing
  of everything except actual driver behavior (ADR 0003).

## 5. Building Block View

*To be completed once `lib/` modules exist — this session only scaffolds
the project layout described in
[`coding-guidelines.md`](../coding-guidelines.md#project-layout), it
doesn't yet contain real building blocks.*

## 6. Runtime View

*To be completed once there's a concrete playback/browsing flow to
describe — next session.*

## 7. Deployment View

Single target: the ESP32-S3R8 primary MCU on the Waveshare board, flashed
via USB-C (CH340 USB-serial). No server/cloud component — fully offline
per project goals.

## 8. Cross-cutting Concepts

- **Hardware/logic separation** — see
  [`coding-guidelines.md`](../coding-guidelines.md).
- **Testing strategy** — see [ADR 0003](../adr/0003-testing-strategy.md).
- *UI/UX concepts (navigation model, screen structure) — to be designed
  next session.*

## 9. Architecture Decisions

See [`docs/adr/`](../adr/README.md) for the full ADR log.

## 10. Quality Requirements

*To be completed — no formal quality scenarios defined yet beyond the
implicit ones in the goals (offline, usable via knob+touch, open source
without licensing risk). Revisit once v1 scope is being implemented.*

## 11. Risks and Technical Debt

(Folds in aim42-style risk/tech-debt practices rather than maintaining a
separate framework — see [ADR discussion](../adr/0001-language-and-framework-choice.md).)

- **Dual-MCU communication is undesigned.** If dual-encoder support or any
  other secondary-MCU feature is wanted later, there's currently no
  researched protocol for how the two MCUs should talk to each other
  outside of Bluetooth use cases. Flagged in ADR 0002.
- **Library/decoder dependency risk.** `ESP32-audioI2S` and LVGL are
  third-party dependencies this project leans on heavily; no fallback
  plan if either becomes unmaintained. Acceptable for now given their
  current activity level, but worth monitoring.
- **No automated hardware regression testing.** On-device behavior
  (display, touch, I2S output) is only verified manually. See ADR 0003.

## 12. Glossary

| Term | Meaning |
|------|---------|
| MCU | Microcontroller unit. This board has two — see [`device.md`](../../device.md). |
| ADR | Architecture Decision Record — see [`docs/adr/`](../adr/README.md). |
| I2S | Inter-IC Sound — digital audio interface used to drive the PCM5100A DAC. |
