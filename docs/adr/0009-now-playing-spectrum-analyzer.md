# 0009: Now Playing spectrum analyzer

## Status

Accepted — 2026-09-14. Changes the no-cover layout and title line budget
of Now Playing from [ADR 0008](0008-braun-design-system-and-screen-redesign.md).

## Context

Audio visualizations were on README.md's out-of-scope list. Trying one
raised three open questions: where it lives (Now Playing or its own
screen), what it looks like under the Braun/Rams design system, and — if it
got its own screen — how to navigate to it and back.

## Decision

### Placement: the cover slot, tap to switch

The spectrum replaces the 96×96 album cover in place. Tapping the slot
switches between cover and spectrum; the choice is persisted in NVS
(`npSpectrum`). With no cover cached the spectrum always shows and the
slot isn't tappable, since there is nothing to switch to.

A dedicated screen was rejected: the only unused gesture is a
right-to-left swipe, which nobody discovers, and it would need page dots
and a new navigation-stack entry (Rams #4, #10). Replacing the cover
permanently would lose album art.

Because the slot is now always filled, Now Playing no longer moves its
text up when there's no cover: the title is always one line and the volume
pill always sits over the slot's center. This amends ADR 0008's "no
placeholder" layout — the spectrum is live data, not a placeholder.

### Look: a neutral dot matrix

12 columns × 12 rows of 6px anti-aliased round dots in 8px cells (exactly
96px), lit from the bottom like an LED level meter. Lit dots are `ink`,
unlit dots `surfaceAlt`. No signal colors (yellow means time, orange means
action), no gradients, no peak-hold markers. Dots fall to zero when paused
instead of freezing (honest, #6).

### Signal path

- **Tap:** `audio_process_i2s` (`Esp32AudioI2SDriver.cpp`) pushes a mono
  downmix into a 1024-sample single-producer/single-consumer ring (atomic
  write counter, no mutex on the audio task's hot path).
- **Read:** `PlaybackDriver::readRecentSamples()` copies the newest
  window and reports sample rate and linear volume gain (a copy of the
  library's volume table). It returns nothing if no new samples arrived,
  which covers pause; `PlaybackStateMachine` also returns nothing unless
  playing.
- **Analyze:** `lib/visualizer/SpectrumAnalyzer.h` (host-tested) — Hann
  window, 1024-point FFT, 12 log-spaced bands 60 Hz–16 kHz, gain divided
  out so volume doesn't change the bars, −60…−6 dB mapped to 0–12 dots,
  instant attack, 300 ms fall.
- **Draw:** `lib/ui-widgets/DotMatrixSpectrum.h` stamps two pre-rendered
  dot bitmaps into one `lv_canvas`, re-stamping only changed columns and
  invalidating only when something changed.
- **Schedule:** `ScreenManager::tickSpectrum()` runs at ~30 fps on the
  main loop (core 1), only while the spectrum is visible, the display is
  on and the device is unlocked.

## Verified on real hardware (2026-09-14)

- Worst-case analyzer + canvas stamp time ~2.0 ms per frame (logged with
  `-DKNOBIFY_SPECTRUM_DEBUG`), well under the 5 ms budget.
- No audible stutter while animating.
- Bands track bass/treble correctly, volume changes don't change bar
  height, pause decays to zero, the unlit grey reads on the panel.
- Tap toggle works and survives a reboot; albums without a (decodable)
  cover show the spectrum by default.

## Consequences

- `PlaybackDriver` gained `readRecentSamples()`; every implementation,
  test fakes included, must provide it.
- `ScreenManager` now takes a `KeyValueStore` for UI settings.
- The spectrum is a second non-transition animation next to the lock
  ring's pulse, justified the same way: it carries meaning and only runs
  while seen.
- The library volume table is duplicated in the driver; if
  ESP32-audioI2S is upgraded, re-check it against `Audio.h`.
