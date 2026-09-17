# 0008: Braun design system and screen redesign

## Status

Accepted — 2026-09-13. Supersedes the color choices in
[ADR 0005](0005-power-lock-and-round-edge-indicators.md) and the battery
placement in [ADR 0007](0007-battery-indicator.md).

## Context

`docs/design/ux-guidelines.md` had adopted a Braun-inspired palette read
through Dieter Rams' ten principles, but the device still ran LVGL's
default light theme with an indigo accent. A review of the guidelines
against Rams' principles found the document itself inconsistent (it
described unimplemented transitions/nudges as existing, double-booked
green, and had two competing "primary" colors), and the screens broke
its own rules: indigo fills on every button, a black lock screen, raw
filenames instead of tag titles, the back button colliding with the
first list row, a mini-bar that looked like a text field, and an
always-on green battery icon.

## Decision

### One theme module, tokens only

`lib/ui/Theme.h/.cpp` is the single source of color truth. Tokens
(`surface`, `surfaceAlt`, `structure`, `ink`, `accent`, `time`,
`confirm`, `warning`) are used everywhere; no raw hex values or
`lv_palette_*` colors in UI code. `theme::apply()` initializes LVGL's
default light theme recolored to the tokens and chains a small child
theme (`lv_theme_set_parent` + `apply_cb`) for app-wide rules the default
theme doesn't cover: screen background, flat unshadowed buttons,
borderless transparent lists, and list rows with a 12px radius, no
dividers, `surfaceAlt` when pressed and `ink` when checked. The existing
`LV_STATE_CHECKED`-based highlight (`ScreenManager::applyHighlight()`)
keeps working unchanged.

### Three button roles

`LvglButtonHelpers.h`'s `makeIconButton`/`makeHoldButton` take an explicit
`ButtonRole`: **Primary** (filled orange circle — Play/Pause, Unlock),
**Secondary** (filled light-grey circle — previous/next), **Quiet**
(unfilled glyph — back, lock, scan). No default, so every call site
states its role.

### Screens

- **Now Playing:** cover, then tag title (up to two lines without a
  cover, one with) and "Artist - Album", round transport row, "elapsed /
  total" time, quiet lock at bottom-center. The back glyph is a down
  chevron ("collapse the player").
- **Song-progress ring (new):** a third `EdgeArc` user, Braun Yellow,
  fed by the new `PlaybackDriver::durationSeconds()` (ESP32-audioI2S's
  `getAudioFileDuration()`, taken under the audio mutex and re-queried
  only when the displayed second changes). Hidden while duration is
  unknown. The orange volume ring temporarily replaces it — one edge
  ring at a time.
- **Lists:** a header zone (quiet back/scan + context caption) with the
  list object placed *below* it, so rows never scroll under fixed
  controls; symmetric row insets; album rows end in the release year.
  The knob-selected row is scrolled into view on every encoder move.
- **Mini-bar:** a full-width `surfaceAlt` bottom area (cut into a
  circle segment by the bezel) with a green playback glyph.
- **Lock screen:** light, like every other screen.
- **Battery:** hidden in normal use, red icon + percent at ≤20% in the
  caption slot, always shown on the lock screen.

### Text line budgets

`ScreenManager.cpp`'s `setClampedText()` gives every label an explicit
maximum line count. LVGL's `LV_LABEL_LONG_DOT` only truncates a label
whose height is fixed; with content height a long title kept wrapping
and overlapped the line below it.

## Found on real hardware during the redesign

- **RGB565 rounds subtle neutrals.** Snow White `#F4F4F0` arrived on the
  panel as neutral `#F6F6F6` (measured from a serial screenshot). Neutral
  tokens are chosen as exact RGB565 values.
- **The panel shifts toward green.** `#EFEFE7`, `#E6E3D6` looked cold,
  `#DEDBC6` (red ≈ green) and `#DED7C6` looked green-yellowish grey.
  `#E6D3BD` / `#D6C3AD` compensated in the other direction but still read
  as wrongly tinted on the device, so the neutrals went back to the
  original Snow White `#F4F4F0` / Light Grey `#DCDDD8` (2026-09-13).
  Judge neutrals on the device.
- **Built-in Montserrat was ASCII-only** (0x20–0x7F plus `LV_SYMBOL_*`):
  a U+00B7 middle dot rendered as a missing-glyph box, so the Now
  Playing separator was " - ", and umlauts/accents had the same
  problem. Resolved in ADR 0019 by generating project-owned text fonts
  covering Latin-1 Supplement and Latin Extended-A; the separator is
  " · " again.
- **Anthracite and black edge rings blended into the dark housing** at
  the bezel, hence signal colors (yellow progress, orange volume).
- **A volume pill positioned over the cover's center covered the title
  when no cover exists**; it now moves into the gap under the back
  button in that case.
- **The selected row's right end ran past the visible circle** with the
  old asymmetric insets (filled shapes show both ends).
- **Knob-driven selection moved off-screen**, since only touch scrolled
  the list view.

## Consequences

- Any new UI must pick a `ButtonRole` and use theme tokens; the
  guidelines (§3, §3a, §6, §7) are the reference for which.
- `PlaybackDriver` gained a method, so every implementation (including
  test fakes) must provide `durationSeconds()`.
- `LV_FONT_MONTSERRAT_28` was compiled in (volume numeral, Play/Pause
  glyph) until ADR 0019 switched the project over to its own
  Montserrat-derived fonts.
- Non-ASCII tag text rendered as boxes until ADR 0019's custom fonts
  with a Latin-1/Latin Extended-A range were generated.
