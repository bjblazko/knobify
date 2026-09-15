# 0011: Shuffle and repeat

## Status

Accepted — 2026-09-15. Replaces ADR 0004's "no auto-repeat in v1"
simplification.

## Context

Playback only played one album in order and stopped after its last track.
The user wanted shuffle and repeat, scoped to the current album, the
artist, or the whole library. knobify has no button to click and a small
round screen that already holds cover, title, transport row, time and lock.
The Rams guidelines prefer context over modes you have to remember, one
accent control per screen, and state shown honestly.

## Decision

### Scope is where you start

Artists, Albums and Tracks lists each start with a **Shuffle** row. Its
scope is the list it's in:

- Artists → the whole library
- Albums → this artist, albums chronological
- Tracks → this album

It plays that scope in random order from a random first track. There is no
scope setting anywhere.

Tapping a track still plays its album in order from that track, and
always switches shuffle **off**. List actions are therefore predictable: the
Shuffle row shuffles, a track plays in order, and no hidden mode changes
what a tap does. The Files tab is unchanged (a tapped file plays alone).

Rejected: a scope switch on Now Playing (Album → Artist → Library). It
would be a mode to remember and another button on a crowded screen.

### Two quiet toggles on Now Playing

Shuffle and repeat sit left and right of the time readout, as *Quiet*
buttons with the 28px Material glyphs. They are grey when off and
`confirm` green when on. They show a state, not a second call to action,
so Play/Pause stays the only accent.

- **Shuffle** reorders the running queue without interrupting the current
  track. On: the rest of the queue is shuffled after it. Off: the original
  order continues after it.
- **Repeat** cycles Off → All → One. The glyph changes to `repeat_one`
  for One.
  - All wraps the queue in both directions, including on finish.
  - One restarts a finished track. Prev/Next still skip (and wrap).

The toggles have no extended hit area and are created before the transport
row, so prev/next win wherever their slop reaches down.

Rejected: modes only in lists/Settings (Now Playing wouldn't show what's
active, Rams #6), and hold-gestures on prev/next (undiscoverable).

### Messages say what a tap did

On the device the green glyph alone didn't tell which mode was active.
Every toggle tap now also shows a message in the new reusable message area
(ux-guidelines §7):

- Shuffle: "Shuffle on - album/artist/library" or "Shuffle off - in order"
- Repeat: "Repeat all", "Repeat this track" or "Repeat off"
- Starting from a Shuffle row: "Shuffling <artist or album>" or
  "Shuffling library"

`ScreenManager` remembers which scope started the queue so the message can
name it.

### What persists

Repeat is a listening preference and is saved in NVS (`repeat`). Shuffle
belongs to the queue: it is set by how playback started and is not saved.

### Code

- `PlayQueue` (`lib/playback/`) keeps the original list plus a play order
  and does the shuffle, wrap and repeat-one logic, host-tested.
- `PlaybackStateMachine` delegates to it. The shuffle seed is
  `esp_random()` from `setup()`.
- `PlaylistBuilder` (`lib/library/`) builds the album/artist/library path
  lists in list-screen order. It uses id-as-index lookups instead of the
  old nested track search, which matters at library scope (~3k tracks).

## Consequences

- Library shuffle holds every track path in memory once more (~300 KB for
  512 tracks, measured). On the device this first exhausted internal RAM and
  broke SD reads, until small allocations were moved to PSRAM in `setup()`
  (AGENTS.md). Storing track ids instead of path copies would shrink this if
  memory gets tight again.
- The toggle placement near the lower bezel still needs confirmation on the
  physical device (ux-guidelines §7).
- Files-tab folder shuffle is not covered; it's on the README backlog.
