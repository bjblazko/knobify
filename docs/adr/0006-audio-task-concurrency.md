# 0006: Dedicated FreeRTOS task for audio decode

## Status

Accepted — 2026-09-13

## Context

`ESP32-audioI2S`'s `Audio::loop()` is a cooperative decoder: it must be
called very frequently (many times per second) or the I2S DMA buffer
underruns and playback stutters audibly. Since [ADR 0001](0001-language-and-framework-choice.md),
it was called once per iteration of `src/main.cpp`'s `loop()`, sharing
that loop with LVGL's display pump and all input handling.

That loop also contains a **synchronous, blocking** display flush
(`LvglGlue::flushCb`, see [ADR 0004](0004-navigation-library-and-index-architecture.md)):
each QSPI transfer to the panel blocks until it completes, by deliberate
v1 design (no async transfer-done callback). Under normal, mostly-static
screens this was rarely long enough to matter. It became audible once
[ADR 0005](0005-power-lock-and-round-edge-indicators.md) added the
lock screen's pulsing hint animation: continuous LVGL redraws meant
continuous flush calls, which measurably starved `Audio::loop()` of CPU
time between calls. Confirmed on real hardware 2026-09-13 — stutter
appeared exactly while the pulse animation ran (and, it turned out,
already existed before this session while scrolling long lists, an
unrelated pre-existing trigger of the same underlying contention), and
disappeared while turning the encoder, which doesn't force extra
redraws.

## Decision

Run `Audio::loop()` continuously on its own FreeRTOS task, pinned to
**core 0** — otherwise idle in this project, since it uses neither WiFi
nor Bluetooth — decoupling audio servicing entirely from whatever
`src/main.cpp`'s `loop()` (LVGL, input, navigation; Arduino's default
`loopTask` runs on core 1) is doing at any given moment.

`Audio` is not thread-safe, so every method that touches it —
`Esp32AudioI2SDriver`'s `playFile`/`pause`/`resume`/`stop`/`setVolume`/
`isRunning`, called from the main task, and the audio task's own
repeated `loop()` call — takes a shared `SemaphoreHandle_t` mutex for the
duration of its call into `audio_`. This was chosen over a command-queue
model (main thread posts play/pause/etc. as async commands, audio task
applies them) specifically to **keep `PlaybackDriver`'s existing
synchronous interface unchanged** — `playFile()` still returns
`connecttoFS`'s real result immediately, `isRunning()` still reads live
state — since main-thread calls into the driver are all short, they
don't meaningfully compete with the audio task's per-chunk `loop()`
calls for the mutex, unlike the blocking display flush they previously
shared a single thread of execution with.

`Esp32AudioI2SDriver::loop()` (the concrete override) becomes a no-op;
`main.cpp` still calls it through the unchanged `PlaybackDriver`
interface for consistency, now doing nothing.

## Consequences

- `lib/drivers-audio/Esp32AudioI2SDriver.h` is the only file that
  changed; `PlaybackDriver`, `PlaybackStateMachine`, and every
  host-tested logic module are untouched — this was achievable entirely
  within the hardware-facing driver layer per
  [ADR 0003](0003-testing-strategy.md)'s hardware/logic separation.
- Not host-testable (FreeRTOS tasks/mutexes don't exist in the `native`
  test environment) — verified on real hardware only, per this driver's
  existing "not host-tested" status.
- If a future feature needs WiFi/Bluetooth, it would contend with the
  audio task for core 0 and needs re-evaluating against this decision.
- `audio_info()`'s diagnostic `Serial.print` (`Esp32AudioI2SDriver.cpp`)
  now runs from the audio task's context rather than the main task —
  fine for occasional diagnostic logging, not a correctness concern.
