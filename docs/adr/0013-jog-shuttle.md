# 0013: Jog/shuttle on Now Playing

## Status

Accepted — 2026-09-15. Amends ADR 0008's "one edge ring visible at a time"
rule with one exception (see below).

## Context

On Now Playing the knob sets the volume, and prev/next skip tracks. There
was no way to move within a track. The user wanted fast forward and rewind
on the knob, like a tape deck's shuttle ring: the knob's position when you
start is normal speed, and the further you turn either way, the faster it
goes. The song-progress ring should keep advancing as before, and a second
indicator should show the shuttle speed from a marker at the top.

Constraints:

- **The knob can't be pressed**, it only rotates (ux-guidelines §5). The
  switch from volume to shuttle has to come from touch.
- **ESP32-audioI2S can't play faster.** `audioFileSeek()` only changes the
  I2S sample rate, up to 1.5×, and the pitch rises with it. What the library
  can do cheaply is jump within the open file (`setFilePos`) without
  reopening it.
- **Seeking only works for MP3 and WAV.** knobify plays `mp3`, `ogg` and
  `wav`; the library can't seek within Ogg.
- **The Rams guidelines prefer no hidden modes**, one accent per screen and
  honest state. The UX guidelines allow only one edge ring at a time.

## Decision

### Hold the time pill and turn

The elapsed-time readout becomes a quiet grey pill: `◀◀ 1:23 / 4:05 ▶▶`
(Material Symbols `fast_rewind`/`fast_forward` from a generated 16 px icon
font `knobify_icon_font_16` with fallback to Montserrat 14 for digits and
text). While you hold the pill,
turning the knob shuttles. Lifting your finger ends it, and the knob sets
the volume again straight away.

The mode only lasts as long as you hold it, like a spring-loaded shuttle
ring, so there is no state to remember or forget. "Center" is wherever the
knob was when you touched down. Letting go always means normal play. The
control sits where its value is shown: you hold the song position to
change it. The gesture is the same hold-and-turn as unlocking (ADR 0005).

While held, the pill switches to its pressed state and the message area
over the cover says what the knob does: "Turn knob to rewind or fast
forward" until the first detent, then the speed, "Fast forward 8x" or
"Rewind 8x" (ASCII `x`: the built-in font has no `×`). The arc alone can't
show the exact step. (First built as `1:23 ▶▶ 8x` inside the pill; the
message is bigger and easier to read, and the hold-and-turn wasn't obvious
without a hint — user feedback on the device, 2026-09-15.)

For a track that can't seek (Ogg), the pill shows no ◀◀ ▶▶ marks and
ignores the hold. It looks like the plain time readout it was before.

Rejected:

- **Tap to toggle shuttle mode.** It creates a mode you have to remember,
  and the knob has no detent at center, so you would have to find normal
  speed by eye.
- **Hold the cover/spectrum.** A big target, but invisible: nothing on
  screen says it exists. A second way to do the same thing was also
  rejected ("as little design as possible").
- **Hold prev/next and turn.** Barely visible, and the button's direction
  competes with the turning direction.

### Fixed steps, CD-style cue

The knob moves through fixed steps, one per detent. Turning past ±5 does
nothing (an end stop):

| Detents from center | 0 | ±1 | ±2 | ±3 | ±4 | ±5 |
|---|---|---|---|---|---|---|
| Speed | normal play | 2× | 4× | 8× | 16× | 32× |

What you hear is CD-style cue: short snippets at normal speed with jumps in
between. Every cue cycle (600 ms, tuned by ear) plays one snippet and then
jumps so the overall rate matches the step. Forward jumps `(N − 1) · cycle`,
rewind jumps `−(N + 1) · cycle`. Both directions sound alike, and you can
tell where you are by ear. Jumps are in milliseconds, computed from the
bitrate, because `setTimeOffset()` only takes whole seconds, too coarse
for 2×.

Rejected: silent scrubbing (you'd navigate by the ring alone), and 1.5×
pitched playback for the first forward step (forward and rewind would
sound different).

### Edges, pause and letting go

- **Stays where you leave it.** Letting go keeps the new position and
  goes back to the play/pause state from before the hold. Shuttling from
  pause plays the cue snippets during the hold and is paused again on
  release, at the new spot.
- **End stops.** Fast forward stops about 1 s before the end of the track
  and stays silent there, so the track can't end during the hold and
  jump to the next one. After release it plays out and moves on as usual.
  Rewind stops at 0:00 the same way. Shuttling never changes the track;
  that stays with the buttons.
- **The hold also ends** on leaving Now Playing, locking, the display
  timing out, or the track changing.

### Shuttle arc: the one exception to one edge ring

While held, two things appear, and both go away on release:

- a small `ink` tick at the top of the ring marks normal speed
- a thin `ink` arc (~5 px) sits just inside the yellow progress ring

The arc runs in LVGL's `LV_ARC_MODE_SYMMETRICAL` over 150°→30° with range
−5..5. It grows from the top: clockwise for forward, counterclockwise for
rewind, 24° per step and 120° at the stop. It has no background track. The
yellow progress ring keeps showing the position and just moves faster.

This breaks the "one edge ring at a time" rule (ux-guidelines §6) on
purpose. Speed and position answer different questions, and you need both
while scrubbing. The arc only shows while your finger is down, and it's
`ink` rather than a signal color: it sits on the off-white surface, not
against the dark housing, and yellow, orange and green already have
meanings. The volume ring can't appear at the same time, because turning
the knob never sets the volume while the pill is held.

Rejected: step dots on each side of the marker, which are more exact but
busier. The speed readout in the pill covers the exact step.

## Implementation outline

- `playback::Shuttle` (`lib/playback/Shuttle.h`, pure logic, host-tested):
  hold/release, step clamping, cue-cycle timing and jump math, end stops
  (a real pause, so track-finished detection can't fire), restoring
  play/pause.
- `PlaybackDriver::seekByMs(int32_t)` in `Esp32AudioI2SDriver` under the
  existing mutex (ADR 0006), via average bitrate and `setFilePos`.
- `PlaybackStateMachine::seekBy()` shifts the wall-clock elapsed time by
  each jump; `canSeek()` decides by file extension (MP3/WAV), so a track
  cued after a reboot already shows its marks.
- `InputRouter` sends encoder deltas to `Shuttle` instead of
  `adjustVolume` while it is held. The pill's press/release callbacks call
  the shuttle directly; `main.cpp` also releases it when the touch ends
  or Now Playing goes away.
- `EdgeArcConfig` gains a `mode` field for the symmetrical arc;
  `makeEdgeArcHost` an inset.
- The pill's ◀◀ ▶▶ marks use `knobify_icon_font_16` (Material Symbols
  `fast_rewind`/`fast_forward`), which has `lv_font_montserrat_14` as
  fallback so digits render in the same label; `line_height` and
  `base_line` are set to Montserrat 14's values (16/3) because LVGL labels
  size and position by the primary font.

## Consequences

- There's a way to move within a track. Skipping still belongs to the
  buttons.
- Ogg tracks can't shuttle. The missing marks say so without a message.
- The pill has to fit between the shuffle and repeat toggles (±100 px), at
  most ~140 px. Long tracks (`12:34 / 45:67` with both marks) are tight.
  If they don't fit on the device, the marks move to a smaller size.
- The cue cycle started at 300 ms. On the device each seek was followed by
  ~290 ms of silence (median gap 580 ms): the library discards its input
  buffer on a seek and stays silent until it is full again, and its 300 KB
  PSRAM default took that long to refill. The input buffer is now 64 KB
  (~80 ms of silence per seek), and the cycle 600 ms, so forward cueing
  sounds like faster playback with small skips rather than stutter.
- The end-stop margin is a constant to tune on the device.
- Elapsed time is still wall-clock based and not read from the decoder
  (ADR 0012), with jumps added on top. Drift from rounding in the
  bitrate-based jumps is possible for VBR MP3s and is accepted.
