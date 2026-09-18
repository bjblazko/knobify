# 0022: A Games menu, and Pong

## Status

Accepted — 2026-09-18. Adds a sixth entry to ADR 0018's main menu, a
first non-audio screen family, and a tone path through ADR 0017's two
decode paths. Suspends one rule of the design system
(`docs/design/ux-guidelines.md` §3) for games only.

## Context

Every destination knobify has had so far plays something through the
speaker: three collections, Settings and the sleep timer (ADR 0018). The
user asked for a sixth, **Games**, and for Pong as its first entry,
"so nah am Original wie möglich".

That request lands unusually well on this hardware, and unusually badly
on this design system.

**Well**, because the original's controller *was* a knob. An Atari paddle
is a potentiometer; this board's entire input surface is one rotary
encoder and a round touch screen. Mapping Pong onto it is not an
adaptation — it is the original arrangement, arrived at from the other
direction.

**Badly**, in three ways, each settled with the user on 2026-09-17:

- Pong is white on black. knobify is a light system: `surface` `#F4F4F0`
  on every screen, lock screen included, because the panel is a
  reflective IPS LCD on which a dark theme rendered poorly (§3).
- The original court is 4:3 landscape with the paddles at the far left
  and right — exactly where a round bezel clips hardest (§7: "never
  anchor UI at a corner", relearned on this device repeatedly).
- Arcade Pong is two-player, two pots. There is one knob.

## Decision

### The court is the inscribed rectangle, and has no corners in it

The largest 4:3 rectangle inside a 360 px circle is exactly **288×216**:
288² + 216² = 360². The court is that rectangle, centred, which means its
four corners touch the bezel precisely and nothing may be drawn at one.

Nothing is. The original has no side walls — a ball leaving sideways *is*
the point — so the only things at the court's edges are the two
horizontal walls, and they run along the top and bottom of the rectangle
where the circle is still 288 px wide. The 4 px walls are part of the
216, so the ball's bounce line is a wall's inner face and the play area
is 288×208 (`PongGame::kCourtHeight`; the outer 216 is
`kCourtOuterHeight`).

This is the one screen whose layout is *derived* rather than measured.
Every other screen in this app got its safe bounds by being looked at on
the device; here the geometry says where the edge is, and the device only
has to confirm it.

### One screen in this app is black

Pong is drawn in white on black, and `lib/ui/ScreenManagerGames.cpp` is
the only file in the project that does not take its colours from
`lib/ui/Theme.h`.

This is a real exception to §3, not a loophole, and it is bounded to
games. The argument is that the palette rule is about *controls*: colour
in this system signals function and operability, and a screen with no
controls on it has no function to signal. A game is a cabinet, not a
front panel — Braun made record players and calculators in this language
and would not have made Pong in it. A light-grey Pong is a picture of
Pong, not Pong.

Rejected: rendering it in `ink` on `surface`. It keeps the rule intact
and loses the thing that was asked for. The user chose the original,
having been shown both.

### The knob's detents are the paddle's zones

Pong's defining mechanic is that the paddle is divided into eight zones,
and which one the ball strikes sets the return angle — steeply away at
the ends, shallow but never flat in the middle. It is what makes the game
one of aim rather than reflex, and the first thing a "better" physics
model loses.

The original's pot is continuous, so it can land the paddle anywhere. A
detented encoder cannot. Rather than let the two grids straddle each
other — 6 px detents across 4 px zones, where the player cannot reliably
choose a return angle at all — **one detent is exactly one zone**
(`kPixelsPerDetent = kPaddleHeight / kZones` = 4 px). Every click of the
knob changes the return angle by exactly one step.

This was found by a test, not by inspection: the zone tests could not
place the ball in an outer zone at all, because no reachable paddle
position put it there. The cost is throw — the full court is about 1.5
turns rather than the paddle pot's three-quarters of one — and that is
the trade the device's own hardware forces.

### The way out is said once, not painted on

Pong is modal: no back button, and this board has no button either, so
the only exit is the app-wide left-to-right swipe. That gesture is learnt
everywhere else from a visible back button, and a game has none -- the
first person to play it simply asked how to stop (2026-09-18).
ux-guidelines §7 already names this failure: "a mode with only an
explicit exit is a mode you can get stuck in".

Entering the game shows one screen message, "Swipe right to leave", in
the same `ink` pill every other message uses. Not a permanent line on the
court: it is needed once, on arriving, and a label telling you how to
stop playing is not part of a game.

### The opponent is capped, not clever

One knob means a computer opponent (the Pong home console's arrangement,
not the arcade's). Its single parameter is a maximum paddle speed, set
just below the ball's steepest vertical speed. It therefore reaches every
ball except a hard-angled one — it loses to exactly the shot the eight
zones reward, which is what makes it beatable without making it look
broken. Two tests pin both halves: a ball played off the end of the
paddle gets past it, and a ball played down the middle does not.

Not done: difficulty levels. One well-chosen cap is the whole design.

### The score is drawn, not typeset

The original has no character generator; its score comes out of a
seven-segment decoder as blocky bars. `ui_widgets::SegmentDigits` draws
them as seven rectangles per digit with a leading zero suppressed. This
is not nostalgia for its own sake: set in Montserrat, the score is the
one element that would announce the screen as a modern UI pretending to
be Pong.

### The game is pure logic; only six rectangles are not

`lib/games/PongGame.h` holds the whole game — ball, paddles, the eight
zones, the AI, serving, scoring — with no LVGL and no Arduino, over an
explicit `nowMs` clock, per `docs/coding-guidelines.md`. All of it is
verified host-side in `test/test_games`, including the trajectories off
each of the eight zones, the speed-up steps, and that a 500 ms stall
cannot put the ball through a paddle.

Positions are integers in 1/16 px and `tick()` splits its elapsed time
into 8 ms slices, so a rally is reproducible in a test and collision
detection stays a plain overlap test.

The screen layer draws it as **plain `lv_obj` rectangles moved with
`lv_obj_set_pos()`**, not a canvas: a full-screen 360×360 canvas would be
259 KB in PSRAM and redraw the whole court for an 8 px ball, whereas
LVGL's own partial invalidation touches only what moved. Heavy redraw
activity is what starves the audio decoder on this board (ADR 0006), so
the frame does as little as it can.

### A rally holds the display awake

`IdleTimer` is reset by touch and encoder activity only. Nothing touches
either during a long point, and when the panel dims `src/main.cpp` stops
calling `LvglGlue::pump()` altogether — so the game would *freeze*
mid-point, not merely dim. `tickPong()` returns whether a rally is
running and `loop()` turns that into activity. The idle timeout still
works between games, which is what it is for.

### Blips are mixed into the stream, not played as a file

The three sounds are gated square waves — literally what the original
produces, since its tones are taps off the counter chain that divides the
7.159 MHz master clock down to video sync. Every pitch is that chain's
~15.7 kHz line rate over a power of two, and `lib/games/PongSounds.h`
uses those divisions. Which division drives which event is taken by ear
and noted as tunable there; published accounts disagree and no schematic
was consulted.

`playback::ToneGenerator` is the generator (pure, host-tested). It is
reached two ways, because ADR 0017's two decode paths converge in two
places and *neither is called when nothing is playing* — which is the
usual state for someone who has opened a game:

1. **While audio runs**, the blip is mixed in at the per-sample seam each
   path already has: `audio_process_i2s()` for the library path,
   `AudioOutputStage::writeFrames()` for the Ogg path. It is scaled by
   the volume like everything else and clips rather than ducking the
   music — 24 ms of square wave, and the original clips too.
2. **While nothing runs**, `drivers::ToneOutput`'s own task on core 0
   pushes the blip to the DAC itself. It decides by watching
   `AudioOutputStage::samplesWritten()`: a counter that has not moved for
   60 ms means no decoder is producing. It claims the I2S rate for the
   duration and hands it back, so the next track is not clocked at the
   blip rate.

   That second path was silent on the device at first, and the cause was
   not in it: the driver's audio task published
   `Audio::getSampleRate()` to `AudioOutputStage` on every iteration,
   **including while stopped**, where the library still reports 16000 --
   a rate nothing is clocked at. It overwrote the 22050 `ToneOutput` had
   just set a millisecond earlier, so the square wave was generated
   against one rate and clocked out at another. The rate is now published
   only while a decoder is actually producing. Found by serial capture of
   the writer's own state rather than by reading the code, which is the
   only reason it was found at all: every value involved looked correct
   in isolation.

A task rather than work in `loop()`, because `writeFrames()` blocks until
DMA takes the frames, and blocking `loop()` is what makes LVGL stutter
and trips the loop watchdog (AGENTS.md).

Rejected: pausing playback on entering a game (simpler, and the user
explicitly wanted the music to keep going); and a second I2S driver
(the port belongs to ESP32-audioI2S's constructor — ADR 0017).

### Games is one menu row and one table

`kMenuEntries` gains a sixth, appended row, since its order is the
`menuVis` byte's bit order (ADR 0018) — the visibility byte already
scales to 8, and the Settings > Main menu row comes for free. `ScreenKind`
gains `Games` and `Pong`, appended past `NowPlaying` so a resume record
can never restore them: waking up inside a game nobody chose would be
startling, and a half-finished rally is not worth saving.

`lib/games/GameCatalog.h` is the list itself. Nothing stores an index
into it, so unlike `kMenuEntries` it may be reordered freely, and a second
game is one row.

`input::ListMoveSink` became `input::KnobSink` with a second method: on
Pong the knob moves a paddle, and a sink named for list movement would
have been lying about it.

## Consequences

- A second game is one row in `kGames`, one `ScreenKind`, and its own
  `render*`/`tick*` pair. Nothing else in this ADR has to be revisited.
- The black-screen exception is now precedent. It is bounded to games in
  §3 deliberately, so the next screen that wants to be dark has to argue
  the same case rather than cite this one.
- `audio_process_i2s()` now does slightly more per sample. AGENTS.md
  records that earlier experiments in that exact hook broke playback in
  ways that looked correct in code, so a playback regression check
  belongs in any change that touches it again.
- The idle blip path works on the device (confirmed 2026-09-18) and does
  not pop, but it remains the least-proven part of this: it writes to an
  I2S port a decoder may claim back at any moment. If it ever misbehaves,
  blips while music plays are independent of it and dropping
  `ToneOutput` is a self-contained retreat.
- `AudioOutputStage::sampleRate()` now means "the rate the DAC is clocked
  at *while audio flows*", and is deliberately stale when stopped. Anything
  that starts trusting it while idle has to set it first, the way
  `ToneOutput` does.
- `BLIP <hz>` joins `TAP`/`KNOB`/`INFO` as a serial command, so the
  pitches can be compared by ear without navigating into the game.
- One thing the geometry cannot settle: whether the bezel really shows
  the wall at the court's top edge. ux-guidelines §7 applies — a
  screenshot does not prove it, and `SCREENSHOT` itself is unreliable
  since the TinyUSB switch (ADR 0016), so it is a question for the person
  holding the device.
