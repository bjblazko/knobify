# 0003: Testing strategy

## Status

Accepted — 2026-09-11

## Context

Embedded firmware is hard to unit-test on the device itself: slow flash
cycles, no host debugger convenience, and hardware dependencies (I2C/SPI/
I2S peripherals, a real display/touch controller) that don't exist off the
board. But most of what makes this project correct — playlist/library
logic, metadata parsing, UI state transitions — has nothing to do with
hardware and shouldn't need a flash cycle to verify.

The project will not be pushed to GitHub in early iterations, so the
testing/CI setup cannot depend on GitHub Actions being the thing that
actually runs checks day to day.

## Decision

- **Hardware/logic separation**: code that talks to peripherals (display
  driver calls, touch controller reads, I2S output, SD card I/O) is kept
  behind thin interfaces. Everything else — playback state machine,
  library indexing, metadata parsing, playlist logic, UI state — is
  written against those interfaces, not the concrete hardware calls, so it
  can run on the host.
- **Host-native unit tests**: PlatformIO's `native` environment (Unity
  test framework, compiled and run with the host's GCC, no board
  required) covers all hardware-decoupled logic. This is the primary,
  fast-feedback test suite.
- **On-device e2e/integration tests**: PlatformIO's `esp32-s3` environment
  covers the things that only make sense on real hardware (does the
  display actually draw, does I2S actually produce audio, does touch
  input register). These are flashed and run manually/locally, not as
  part of the automated suite, since there's no CI runner attached to the
  physical board.
- **Local-first CI**: `scripts/check.sh` runs the native unit tests and a
  firmware compile check (build the `esp32-s3` env without flashing) and
  is the actual day-to-day verification loop, runnable with no GitHub
  involved. A GitHub Actions workflow (`.github/workflows/ci.yml`) runs
  the same two checks and exists so CI is ready the moment the repo is
  pushed remotely, but nothing about local development depends on it
  running.

## Consequences

- Most logic bugs get caught fast, on the host, without touching the
  board.
- Driver/hardware correctness is only checked manually on-device — no
  automated regression protection there yet. Acceptable for the current
  solo/local-first stage; revisit if/when a self-hosted CI runner with the
  board attached becomes viable.
- Requires discipline to keep hardware calls behind interfaces rather than
  scattered through application logic — enforced via code review /
  self-review against [`coding-guidelines.md`](../coding-guidelines.md).
