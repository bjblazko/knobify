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
- Shuffle and repeat: a Shuffle row at the top of the Artists, Albums and
  Tracks lists shuffles the library, the artist or the album. Toggles
  in Now Playing's options panel switch shuffle and repeat (off / all /
  one) — see [ADR 0011](docs/adr/0011-shuffle-and-repeat.md).
- Now Playing options panel: a handle at the bottom opens a panel with
  shuffle, repeat, the cover/spectrum switch and lock, keeping the player
  itself uncluttered — see
  [ADR 0014](docs/adr/0014-now-playing-options-panel.md).
- Resume after power loss: the device comes back on the last screen with
  the last track paused near where it was (no auto-play). State is saved
  periodically and power-cut safe — see
  [ADR 0012](docs/adr/0012-resume-session.md).
- Jog/shuttle: hold the time readout on Now Playing and turn the knob to
  fast forward or rewind (five speeds each way, CD-style cue); letting go
  plays on from there — see [ADR 0013](docs/adr/0013-jog-shuttle.md).
- M4A (AAC) plays natively with tags, exact durations and embedded covers;
  progressive JPEG covers decode too. Settings > USB drive exposes the
  SD card to a computer over the USB cable — see
  [ADR 0016](docs/adr/0016-native-formats-and-usb-drive.md) (proposed).

## Explicitly out of scope for now

- Bluetooth headphone output
- Wi-Fi-based features (time/date sync, weather, podcasts, internet radio)
- Ogg Vorbis playback (ESP32-audioI2S 2.3.0 has no Vorbis decoder;
  stb_vorbis runs 2.5x realtime on the device — a second decode path,
  see ADR 0016) and 24-bit FLAC
- Formatting the SD card from Settings with 32 KB clusters, which USB
  drive mode needs on macOS (ADR 0016)
- A desktop sync tool (mirror a folder to the knob, delete removed
  albums) on top of USB drive mode
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
- Shuffle for a Files-tab folder (library shuffle by album/artist/all
  exists, see ADR 0011; tapping a file still plays it alone)
- Volume normalization / ReplayGain-style loudness matching across
  tracks
- EQ presets (bass/treble/flat, etc.) built on the library's existing
  `setTone()` — see AGENTS.md's note on the volume-boost attempts before
  reusing it
- Favorites/playlists (marking tracks or albums for quick access)
- A "recently played" / "recently added" quick-access list
- Resume support for future sources (podcasts, web radio, video) — each
  adds its own section to the resume record, see ADR 0012
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

## License

knobify is licensed under **GPL-3.0-or-later** — see [LICENSE](LICENSE).

That follows from the audio library: ESP32-audioI2S is GPL-3.0, so
firmware linking it is covered as a whole. [THIRD-PARTY.md](THIRD-PARTY.md)
lists every component and its licence, and explains where M4A/AAC decoding
comes from (the Helix-derived decoder inside that same library) and how
its patent situation looks — knobify ships no decoder of its own.

## Starting a new session here

The next step is UX/design work for v1 (navigation model, screen
structure, library/metadata handling on the SD card), followed by actual
feature implementation on top of the existing scaffold. A good first
prompt for that session:

> Read README.md, device.md, docs/adr/, and docs/arc42/arc42.md. Let's design the UX for v1 (navigation model, screen structure for browsing + now-playing, how the rotary encoder and touch interact) and then start implementing it inside the existing PlatformIO scaffold, following docs/coding-guidelines.md.
