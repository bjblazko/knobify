# 0023: Gravity, and drawing a game in lines

## Status

Accepted — 2026-09-18. The second entry in ADR 0022's Games menu. Extends
`playback::ToneGenerator` with a noise source and a sustained mode, and
replaces `KnobSink::onPaddleMove()` with one method for every game.

## Context

ADR 0022 shipped the Games menu with one entry and a table built so a
second game would be a single row. This is that row: set a craft down on
a flat pad before the fuel runs out, with the knob turning it and a held
finger firing the engine.

It is a Lunar Lander, named for the same reason Table Tennis was — "Lunar
Lander" is an Atari mark, "gravity" is a dictionary word. The lineage is
public: Jim Storer's 1969 text game, which the 1979 arcade cabinet made
graphical.

Three choices were taken with the user, each the more faithful of the
options offered: noise for the thrust rather than a beep, the arcade's
full instrument readout rather than a minimal one, and landing pads with
multipliers rather than "any flat spot counts".

## Decision

### Lines, not rectangles — and that is the faithful answer

Table Tennis is six rectangles because a ball and a paddle *are*
rectangles. A lander is not: it rotates, and **LVGL 8 applies
`transform_angle` only to images, so a rotated plain object renders
upright** — a limit ADR 0018 first hit on the wordmark's dial.

So the craft is an `lv_line` whose points are recomputed every frame.
That is not a workaround dressed up as a decision: the 1979 machine is a
vector display, so line art is what the game actually looked like. Three
line objects carry it — the ridge of the landscape, the craft's outline,
and the exhaust — and `lv_line` holds a pointer to the caller's points,
so the arrays live on `ScreenManager` and nothing is allocated per frame.

`games::FixedTrig` rotates them: a quarter-turn sine table in 1/1024,
integer throughout. The same call gives the thrust direction and the
drawn outline, so what you see and what the physics does cannot drift
apart, and a whole flight replays identically in a test.

### The hillside is 90 columns, because a canvas is too expensive

The ridge line alone read as a wireframe; the ground wanted to be solid
(user, 2026-09-18). LVGL 8 cannot fill a polygon outside a canvas, and a
canvas covering this landscape is about 100 KB — which must live in
PSRAM and be re-blitted on every redraw that overlaps it.

Instead the ground is 90 rectangles, 4 px wide, each running from the
terrain's height at its own centre to the bottom of the framebuffer. They
step, but by less than the ridge line drawn over them is wide, so the
steps do not show. A `static_assert` keeps them tiling the screen exactly.

That fill is also why the landscape is now **smoothed**: heights were
free random values 12 px apart, which is a sawtooth rather than a
gebirge, and a stepped fill beside a 70° ridge would have been obvious.
Neighbours now differ by at most 18 px, smoothed in both directions so
the wrap seam is no steeper than anywhere else. It flies better too.

### The round screen constrains the pads, not the landscape

Unlike Table Tennis's court, this world has no edges that must be seen:
terrain and sky simply continue past the bezel and cropping them looks
like more ground. Two things must stay inside the circle, and both are
rules in code rather than hopes:

- **Every pad.** `GravityTerrain::generate()` takes a `HalfWidthFn` and
  rejects any placement whose ends fall outside it, with a 12 px margin
  because the bezel clips harder than the framebuffer suggests. A test
  generates 200 landscapes and checks every pad against a real circle —
  and checks that at least one pad survives, since a rule that rejects
  everything is not a rule, it is a bug.
- **The instruments**, top-centre, in two rows of two. The very top of
  this screen is too narrow for two columns; both rows sit lower
  (ux-guidelines §7).

The craft wraps horizontally. An invisible wall on a screen with no
visible edge is the worse of the two.

### Four separate questions, not one verdict

A landing is accepted only if it is on a pad, upright within 10°, under
the descent limit and under the (tighter) drift limit. Each is a separate
public predicate, which buys two things: four tests that fail one at a
time, and a screen that can say **which** — "MISSED THE PAD", "CAME DOWN
TILTED", "CAME DOWN TOO FAST", "STILL DRIFTING". "Crashed" alone teaches
nothing, and this is a game about learning a control.

Score is the pad's multiplier times what is left in the tank, so a
cautious landing on a wide pad and a tight one on a narrow pad are
genuinely different choices rather than the same one scored differently.

### Integer integration has to carry its remainder

Positions and velocities are 1/16 px like Table Tennis's, and `tick()`
splits elapsed time into fixed sub-steps the same way. That was not
enough.

A drift of 10 px/s is 160 sub-units per second, and a 5 ms slice of that
is 0.8 — which truncates to **zero**, every slice, forever. Traced on a
flight: the craft's sideways drift did not exist, and a gentle descent
hovered at a fixed height until the tank ran dry. Table Tennis never hit
this because a ball moves an order of magnitude faster than a lander; a
game about arriving *slowly* lives exactly in the range that rounds away.

Every integration now carries its remainder, which is exact over time and
stays in integers — which every test here depends on.

### The engine is a state, not an event

`GravityGame::Sound` carries one-shots only: touchdown and crash. Thrust
was queued as on/off events at first, and a normal descent filled the
six-slot queue with pairs and pushed the touchdown out of it — the one
sound that had to survive. The screen reads `thrusting()` instead and
starts or stops the noise from what the game *is* doing.

### The tone generator grows noise, and a note that holds

A rocket is a rumble; a square wave is a beep however low it is pitched.
`ToneGenerator` gains a 16-bit maximal LFSR — one bit per sample, sign
only — and a sustained mode where `durationMs == 0` plays until
`silence()`. Thrust lasts as long as the finger does, which no fixed
duration can express, and a note re-triggered every frame would stutter.

Adding noise turned up an error in the tone path that had been there
since ADR 0022: `sampleRate / (2 * frequency)` truncates, so a 1400 Hz
request came out at 1575 Hz. It happens to divide exactly at 441 Hz,
which is why Table Tennis never showed it. Pitch now comes from a 16.16
phase accumulator, and the existing blips hit their frequencies for the
first time.

### One knob method for every game

`KnobSink::onPaddleMove()` becomes `onGameKnob()`, and `InputRouter`
routes every game screen to it: the sink would otherwise grow a name per
game, and only the screen showing knows what a detent means there.
`ScreenManager` dispatches on the current screen.

Getting that dispatch wrong is how the knob did nothing at all in Gravity
on the first build — the single implementation still checked for Table
Tennis's ball and gave up. Worth recording because the renaming looked
complete and compiled cleanly. Rotation also works **before** the launch,
not only during the flight: a control that does nothing until you have
started reads as a broken control.

### Said once, while there is time to read it

Three controls is two more than a knob and a touch screen suggest on
their own — turning does not look like steering, and a screen you *hold*
is not a screen you tap. Before the launch the sky carries "TURN TO
STEER / HOLD TO THRUST", and the way out arrives as the same one-off
screen message ADR 0022 uses. All of it disappears once the flight
starts.

The throttle takes `LV_EVENT_PRESS_LOST` as well as `RELEASED`: a finger
that slides off the edge must not leave the engine burning.

## Consequences

- A third game is still one row in `kGames`, one `ScreenKind` and its own
  `render*`/`tick*` pair. `GameScreenStyle.h` now holds what the games
  share, and `ScreenManagerGames.cpp` became
  `ScreenManagerTableTennis.cpp` — one file per game.
- `ToneGenerator`'s sustained mode means `ToneOutput`'s idle writer can
  now run indefinitely. It already stood down the moment a decoder
  produced anything, so nothing changed there, but a held note is the
  first thing that exercises that path for more than a fraction of a
  second.
- Physics and sound run every loop iteration and only the drawing is
  gated, exactly as ADR 0022 concluded. That lesson transferred without
  having to be relearned, which was the point of writing it down.
- Not settled on the device yet: whether 6° per detent is the right
  rotation, and whether four numeric readouts can be watched at the same
  time as the craft. Table Tennis's knob took three attempts; this one
  should be expected to take some.
- Deliberately absent: the arcade's scrolling, magnifying landscape, and
  any persistence of score. Both are in the README's out-of-scope list
  rather than half-built.
