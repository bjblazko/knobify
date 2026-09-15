# knobify

Offline music player built on a Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — see [`device.md`](device.md) for full hardware specs (dual MCU, display, audio DAC, encoders, etc.), the official product page, and wiki links.

## Goal

See [`docs/design/ux-guidelines.md`](docs/design/ux-guidelines.md) for the
UX/UI design philosophy, color system, and interaction flows behind the
screens described below.

- Play music stored on an SD card, in well-known formats.
- Audio output via the onboard 3.5mm jack (PCM5100A DAC).
- Control via the board's rotary encoder(s) + touch display.
- Fully offline for v1.
- Display power and device lock, tuned for two real usage contexts: on a
  table (display times out on its own, a touch wakes it without acting
  on whatever's underneath) and in a pocket while playing (manually
  locked so touch/knob can't trigger anything by accident; unlocking
  requires holding the on-screen unlock button while turning the
  encoder — see [ADR 0005](docs/adr/0005-power-lock-and-round-edge-indicators.md)).
  Song progress, volume and unlock progress are shown as rings hugging
  the round display's edge, via a small reusable widget.
- Battery level shown only when it matters: red with percentage at 20%
  or below, and always on the lock screen — see
  [ADR 0007](docs/adr/0007-battery-indicator.md) and
  [ADR 0008](docs/adr/0008-braun-design-system-and-screen-redesign.md).
- A Braun/Dieter-Rams-inspired visual design (warm off-white surface,
  one orange primary control per screen, tag titles, album cover and
  release years) — see
  [the UX guidelines](docs/design/ux-guidelines.md).
- A dot-matrix spectrum analyzer in Now Playing's cover slot: tap the
  cover to switch, shown by default when an album has no cover — see
  [ADR 0009](docs/adr/0009-now-playing-spectrum-analyzer.md).
  No charging indicator: the board exposes no charge-status signal (no
  dedicated pin, no voltage change on plug/unplug, no status LED).

## Explicitly out of scope for now

- Bluetooth headphone output
- Wi-Fi-based features (time/date sync, weather, podcasts, internet radio)
- USB mass-storage ("drive") mode for copying music without swapping the
  SD card — the ESP32-S3's native USB-OTG could expose the SD card as a
  USB drive via TinyUSB's MSC class while plugged in, so the card
  wouldn't need to be physically removed and read on another computer
- Jog/shuttle-style scrubbing through a track's playback position
- Theming (selectable color schemes / customizable look)
- General visual polish and animation ("eye candy") beyond the planned
  one-time gesture-hint nudge and screen-transition slide
- Voice memo / dictation recording via the onboard PDM microphone
- A richer Now Playing screen (more detail/interactivity beyond the
  current controls + elapsed time)
- Using the rotary encoder as a jog dial for scrolling long lists/menus
  faster (beyond the current one-item-per-detent behavior)
- General UX polish pass once the above land and real usage patterns are
  clearer
- Charging state indicator (no signal available to detect it — see
  [ADR 0007](docs/adr/0007-battery-indicator.md))
- Shuffle and repeat modes (per-album/all-library shuffle, repeat
  single/all) — decision 5's "no auto-repeat in v1" was a deliberate v1
  simplification, not a permanent rule
- A sleep timer (auto-stop playback after a set time)
- Volume normalization / ReplayGain-style loudness matching across
  tracks
- EQ presets (bass/treble/flat, etc.) built on the library's existing
  `setTone()` — see AGENTS.md's note on the volume-boost attempts before
  reusing it
- Favorites/playlists (marking tracks or albums for quick access)
- A "recently played" / "recently added" quick-access list
- Resuming playback position after a restart or power loss, not just
  the persisted volume
- M3U playlist file import
- Gapless playback (for live albums, concept albums, etc.)
- On-device firmware updates from a file on the SD card (no Wi-Fi
  needed, fits the offline-first goal)
- Jump-by-letter navigation for long lists (e.g. holding the encoder's
  equivalent gesture while turning jumps through initial letters)

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
