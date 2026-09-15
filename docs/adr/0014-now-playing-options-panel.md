# 0014: Now Playing options panel

## Status

Accepted — 2026-09-15. Amends ADR 0005 (lock button placement), ADR 0009
(tap the cover to switch) and ADR 0011 (shuffle/repeat toggles beside the
time).

## Context

Jog/shuttle (ADR 0013) turned the time readout into a 28 px pill. Below the
title, Now Playing then stacked three rows: the transport row (72 px), the
pill with the shuffle and repeat toggles beside it, and the lock button
(44 px touch target). They filled the 144 px between the artist line and
the bezel exactly, with no gap left. On the device taps kept landing on
the wrong control, and the pill's touch area overlapped the lock's.

Shuffle, repeat, lock and the cover/spectrum switch are all used far less
often than Play, skip and the song position. The back chevron at the top
had to stay centered.

## Decision

### A handle and a panel

Now Playing keeps only the song, the transport row and the time pill. A
small `︿` handle sits bottom-center, mirroring the back `⌄` at the top.
Tapping it slides a panel up over the lower half (200 ms ease-out) with
four secondary circles in a row, each named underneath:

- **Shuffle** — glyph `confirm` green while on (ADR 0011).
- **Repeat** — off / all / one, same glyphs and green as before.
- **Cover / Spectrum** — shows what a tap switches to (Material `image` /
  `equalizer`). Greyed out and inert when the album has no cover.
- **Lock** — locks and closes the panel.

The toggles still show their message ("Shuffle on - album") in the cover
area, which the panel leaves visible. The panel closes with its own `⌄`,
a tap outside it (a transparent scrim, so that tap never also hits Play or
the pill), locking, or leaving Now Playing. The knob keeps setting the
volume while it is open.

The cover slot itself is no longer tappable: the switch lives in the panel
only. Nothing on screen said the cover was tappable.

Rejected:

- **Lock next to the back chevron at the top.** Moves the back button off
  center.
- **A "⋯" button instead of the lock.** It needs a full-height row again,
  so the transport row would have to shrink.
- **Tap the cover to open the panel.** Most space, but nothing on screen
  says it exists.
- **Dropping the artist line, or a smaller transport row.** Buys only a
  few pixels each.

### Layout

Cover, titles and transport moved up (`kCoverY` 56 → 44, title 2 px closer
to the cover, `kTransportCenterY` 246 → 236). Play/Pause to pill is 14 px,
pill to handle about 10 px. The pill is created after the handle so it wins
where the handle's touch slop reaches it.

## Consequences

- Locking takes two taps instead of one. The lock itself (hold and turn to
  unlock, ADR 0005) is unchanged.
- Shuffle and repeat state is no longer visible at a glance on Now Playing;
  opening the panel or the message after a toggle shows it.
- Two glyphs (U+E3F4 `image`, U+E01D `equalizer`) were added to the 28 px
  icon font; the existing glyph bitmaps are byte-identical.
- The panel's open state survives a re-render (its toggles re-render the
  screen) and is only animated when opened. Swipe gestures for opening and
  closing were left out; tap is enough for now.
