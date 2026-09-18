# 0024: Tones, a tone generator, and the shared signal layer

## Status

Accepted — 2026-09-18. Adds a seventh entry to ADR 0018's main menu and a
second source to ADR 0022's idle audio writer. Fixes a pitch bug in
`Esp32AudioI2SDriver::resume()` that ADR 0022's blips had introduced.

## Context

The user asked for three related tools: a **tone generator**, a **voice
recorder** and a **spectrum analyzer** fed by the microphone or by
knobify's own output. Only the first is built here. The request came with
an instruction for how to build it: the three share components — a signal
path and a scope/spectrum display — and the first one should be cut so
the other two can reuse its pieces.

Two things about this board shape the design:

- The knob only rotates (ux-guidelines §4). The user asked for "mode
  buttons" so that one screen can set frequency, level and the wave's
  shape. On this board those buttons have to be touch targets.
- The DAC already has one writer outside the decoders: `ToneOutput`, the
  task that pushes a game's blips out when nothing is playing
  (ADR 0022). A second writer would have to negotiate the I2S port with
  it.

Each decision below was put to the user as a question with options, and
answered, before any code was written.

## Decision

### A shared, pure `lib/signal/`

Everything that is not about this one screen lives in `lib/signal/`,
namespace `knobify::signal`, with no Arduino and no LVGL, host-tested:

| Piece | What it does | Reused by |
|---|---|---|
| `Oscillator` | Sine, square, saw, noise into a buffer at any rate | recorder (monitor tone), analyzer (test signal) |
| `TriggeredScope` | Sample window → a reconstructed trace that stands still | recorder, analyzer |
| `ScopeScale` | Stepped timebase and level range, with hysteresis | recorder, analyzer |
| `Spectrum` | Absolute dBFS, log axis 20 Hz – 20 kHz | analyzer |
| `SampleSource` | "the newest samples, if any are new" | the microphone will implement it |
| `GeneratorControl` / `GeneratorOutput` | Lock-free handover to the audio task | — |
| `ToneSettings`, `ToneSession` | This screen's model and lifecycle | — |

`ui_widgets::ScopeTrace` is the drawing half of the scope, and equally
unaware of where its samples came from.

`playback::ToneGenerator` (the games' blips) stays as it is. A gated
square wave with a duration is a different job, and it has its own
tests.

### Chips choose what the knob sets

Below the scope sits a row of chips: the waveform, **Hz**, **dB** and,
where the waveform has one, a shape parameter (**Duty** on a square,
**Shape** on a saw). Tapping a chip selects it (filled `ink`, like a
selected list row), the selection stays, and the knob adjusts it. The
waveform chip is a chip like the others: selected, the knob steps
Sine → Square → Saw → Noise without wrapping.

- **Selected on the press, not the click.** Holding a chip and turning
  the knob with the other hand is this device's two-handed gesture
  (§7, ADR 0013, ADR 0021). The chips are therefore created once per
  render and only shown, hidden or moved on a knob turn, never
  recreated: a chip under a finger must survive the turn.
- **A chip appears only where it acts** (§7). Noise has no Hz chip;
  Sine and Noise have no shape chip. If the selected chip disappears,
  Level stands in for it.
- The large value above the chips always shows the selected one, in
  words where words say more: a saw's shape reads *Rising*, *Triangle*,
  *Falling*, or *Shape 35 %* in between.

Rejected: a fixed knob meaning (frequency) with hold-to-adjust for the
rest, which is fiddly for a second parameter; and one page per parameter,
which hides the relationship the scope is there to show.

### Frequency: a log grid, and speed decides the step

Frequency moves on a **1/48-octave grid anchored at 1 kHz**, from step
−270 (20.26 Hz) to +207 (19.87 kHz). Anchoring at 1 kHz makes the default
exact. The cost is that 440 Hz is not on the grid; the nearest step is
439.0 Hz. The user chose this over semitone steps and over a digit
cursor.

One slow detent is 1/48 octave. Faster turning takes bigger steps, judged
from detents per second since the previous turn. Below 10/s a detent is
one step; from 10/s it is 4; from 25/s it is 16 (1/3 octave). A pause of
250 ms or more always counts as slow. Level (1 dB per detent, up to 3
when fast) and the shape parameters (1 % per detent, up to 5) use the
same speed tiers with their own step sizes. The waveform always moves one
at a time. The user tried the tiers on the device and changed none.

### Noise comes in five colours

Noise has a **Color** chip, like Duty on a square: brown, pink, white,
blue, violet, i.e. −6, −3, 0, +3 and +6 dB an octave. The user asked for
white, pink "and whatever else", and chose all five over the three common
ones. They are a chip and not five more waveforms, which keeps the
waveform list short. The knob turns **darker to brighter**, so turning
right makes noise hiss more, as it raises a pitch. It moves one colour a
detent however fast it is turned. White stays the default.

- **Equally loud.** Every colour is built from the same Gaussian white
  (four uniforms summed) and filtered back to the same RMS. Switching
  colour therefore does not switch loudness.
- **The level still means the peak.** The RMS sits 12 dB under the level,
  and the rare sample past 4 sigma (about one in 16,000) is clipped. For
  noise, as for a tone, −20 dB means "never above −20 dBFS". The first
  noise was uniform white at full level, which was louder. It changed to
  this when the colours came.
- **Filters:** Paul Kellet's refined pink filter (−3 dB an octave to
  within 0.05 dB above 9 Hz). A leaky integrator for brown (leak 0.998,
  so it falls from ~8 Hz and never drifts to a rail). Blue and violet
  are pink and white differentiated. Each filter's gain back to unit RMS
  was measured over 100 s of its own input.
- The tests hold every colour to its slope over four octaves (±3 dB) and
  to the same RMS (±1.5 dB), and none past its level.

On the spectrum page a colour shows as a tilt. The page draws the loudest
bin in each column, and high columns hold more bins, so noise reads a
few dB brighter there than its true density. An RTA-style band-power
view, on which pink is flat, would be the analyzer's decision.

### The level is dBFS, not a volume

Level runs from −60 to 0 dBFS in whole dB. It is **independent of the
device volume and of the sleep timer's output gain**: the number on screen
is what leaves the jack. On first use it is −20 dB, loud enough to hear
and safe for headphones.

To get there, `AudioOutputStage` has a second write path,
`writeFramesUnscaled()`: no volume, no output gain, but still recorded in
the sample ring so the scope sees it.

The sleep timer fades by lowering that output gain, which this path
ignores. The generator therefore **stops** when the fade begins instead
of fading with it.

### One writer: the idle task plays the generator too

`ToneOutput` already owns the port whenever no decoder produces. It now
has two sources. While the generator is running, or still fading out
after a stop, the task claims **48 kHz** and writes `Oscillator` chunks.
Otherwise it writes blips at 22.05 kHz as before.

It remembers which rate it last claimed, so switching between the two
sources reprograms the clock and repeating one does not. Blips and the
generator never coincide, because no game runs while Tones is open.

Rejected: a second task for the generator, which would have had to
arbitrate the port with the first.

### A voice that does not click

- **Phase-continuous.** A 32-bit phase accumulator; changing pitch mid-
  wave never restarts the cycle.
- **PolyBLEP on jumps.** At 48 kHz a naive square or saw edge aliases
  audibly from a few kHz up. PolyBLEP smooths the square's two edges and
  the pure saws' one jump for two multiplies per edge. The saw's
  in-between shapes have corners, not jumps, and stay naive.
- **5 ms ramps** on every level change, on start and on stop. The task
  keeps writing until a stop's ramp reaches silence, so the last thing
  the DAC hears is zero rather than a cut.
- The sine comes from a 1024-entry table with linear interpolation.

On the device, with the user listening on the jack, nothing clicked on
start, stop, fast turning or a waveform change.

### The scope shows what reached the DAC

The scope draws the samples that actually went out, from the output
stage's ring. It does not draw an idealised picture of the waveform. It
triggers on a rising zero crossing with hysteresis, so a periodic signal
draws the same picture frame after frame.

- **The ring grew from 1024 to 4096 samples** (+6 KB internal RAM) so
  low tones fit: 4096 samples at 48 kHz are 85 ms. Measured after the
  change: 96 KB of internal RAM still free.
- **It reads through `SampleSource`, not the player.**
  `PlaybackStateMachine::readRecentSamples()` answers only while music
  plays, and the generator pauses music before it sounds, so the scope
  was first seen flat under a measured 1 kHz tone. `ToneOutput`
  implements `SampleSource` over the ring. The microphone will implement
  the same interface.

### The scales step like a bench scope's knobs

The first scope always fitted the signal: two periods wide, the set level
tall. So 200 Hz and 400 Hz, or −20 dB and −26 dB, drew the same picture,
and only the number said anything had changed. The user asked to *see* a
doubling. `ScopeScale` now holds both scales in steps, the way a bench
scope's time/div and volts/div knobs do:

- **Timebase in 1-2-5 steps** from 100 µs to 50 ms (the band's full
  width). A fresh choice shows 2 to 5 periods. Within a step, doubling
  the pitch doubles the waves on screen. The step changes only once the
  picture gets too sparse (under 1.6 periods) or too crowded (over 6.25).
- **Level range in 10 dB steps**: the band's edge sits at 0, −10 … −60
  dBFS. Within a range, +6 dB draws twice as tall. The range steps up as
  soon as the level passes its edge, and steps down only once the level
  is 13 dB under it (a quarter of the height). A level turned back and
  forth across a boundary therefore does not make the picture jump back
  and forth.
- **A label under the band names both scales**, e.g. `5 ms · -20 dB`:
  the band's width and the level at its edge.
- Noise has no period, so its timebase is chosen as if it were 100 Hz
  (20 ms).

### Between samples the trace is reconstructed

Straight lines between samples drew a 15 kHz sine (3.2 samples a period)
as a zigzag, and 19.9 kHz as barely a wave. The trace is now a
**windowed-sinc reconstruction**: 16 taps either side under a squared
Welch window, which is what the DAC's own reconstruction filter does. So
the trace shows what comes out of the jack: 19.9 kHz is a clean sine at
the 200 µs step.

The trigger position is refined on the reconstructed signal as well.
Placed by a straight line between two far-apart samples, it made high
tones shimmer. The cost is 32 multiplies per point, 160 points a frame:
the worst scope frame measured 3.8 ms.

This is honest in both directions. A 15 kHz square reads almost as a
sine, because its harmonics above 24 kHz do not exist at 48 kHz. A square
or saw edge shows the ringing the reconstruction filter really puts on
it.

### Swiping the band turns it to a spectrum

The band has a second page: a spectrum (`signal::Spectrum`) from 20 Hz to
20 kHz on a log axis, in absolute dBFS from 0 at the top to −80 at the
bottom. A −20 dB tone stands at −20 dB, and a square's odd harmonics
stand where the maths puts them. It is not the Now Playing analyzer,
which lights twelve bands of a dot matrix to music with the volume
divided out.

- **Hann window over 2048 samples**, i.e. 23 Hz a bin. That is coarse in
  the bottom octaves, where a column falls between bins and is
  interpolated, and plenty everywhere else. A tone off a bin's centre
  reads up to ~1.4 dB low.
- **DC is removed first, weighted by the window.** 0 Hz is off the axis,
  but the window smeared an off-centre square's DC across the lowest
  columns, where it read on the device as a hump of bass at 20–45 Hz. A
  plain mean still left −53 dB there.
- **Levels fall at 60 dB/s** rather than vanishing.
- **Cost:** ~5 ms a frame on the device. The first frame takes 50 ms
  once, while the ~32 KB of buffers are allocated. They are allocated
  on first use and released when the screen is left, so they come from
  PSRAM.
- **The gesture:** swiping right to left inside the band shows the
  spectrum, left to right the scope, like turning a page. Two page dots
  under the label say there are two. The band takes its own swipes: it
  is reported by `swipeStartsOnControl()`, so the app-wide back swipe
  does not start there. Back stays on the chevron, or on a swipe
  anywhere else. The user chose this over tapping the band.

### The touch release point is where the finger lifted

The swipe first turned only one way. `LvglGlue` passed LVGL the
coordinates of the *released* touch sample, which carries no position of
its own (it read x=11 wherever the finger had been). LVGL takes the
release point as where the finger lifted. The glue now reports the last
pressed point on release, as touch drivers do. This applies app-wide.

### A tone plays alone, and never on its own

- **▶ pauses music** that is playing, then starts. A tone is for
  measuring, and mixing it over a track (as the game blips are) defeats
  that. The track is paused, not stopped, so it resumes afterwards.
- **The screen opens silent.** Leaving it by any route stops the tone,
  and so do locking and the sleep timer. `ScreenKind::ToneGenerator` is
  appended past `NowPlaying`, so a resume never lands on it: a device
  that wakes up and makes a noise would be startling.
- **Settings persist.** Waveform, frequency, level, duty and symmetry are
  kept across reboots in six NVS bytes (`tgWave`, `tgFreqHi`, `tgFreqLo`,
  `tgLevel`, `tgDuty`, `tgSym`). They are saved 2 s after the knob comes
  to rest, and when the tone stops, not on every detent.

### Resuming a track restores its rate

`Esp32AudioI2SDriver::resume()` now sets the track's own I2S rate before
resuming. Neither the library's `pauseResume()` nor the Vorbis backend
sets it again. Since ADR 0022, a track paused before a game resumed at
the blips' 22.05 kHz, i.e. at half speed. After a tone it would have
played about 9 % fast. The user checked both cases on the device after
the fix.

### Measured on the device

With `-DKNOBIFY_GENERATOR_DEBUG`, `ToneOutput` logs the pitch and peak it
measured from the samples it wrote, once a second:

| Setting | Logged |
|---|---|
| Sine 1 kHz, −20 dB | 1000.0 Hz, peak 3277 (−20.0 dBFS) at 48000 Hz |
| Lowest step (20.26 Hz) | 20.0 Hz (whole crossings per second) |
| Highest step | 19870 Hz |
| 0 dB | peak 32736 (−0.0 dBFS) |

## Consequences

- The recorder and analyzer start from a tested oscillator, scope, scope
  widget and sample-source interface. What they add is a microphone
  driver implementing `SampleSource` and, for the analyzer, a spectrum
  finer than Now Playing's 12 bands.
- The Home tile uses Material Symbols `airwave` (U+F154), added to both
  icon fonts: the carousel draws its side tiles from the 28 px one. Both
  fonts' existing ranges regenerate byte-identical from today's source
  font.
- The generator is the one sound source that ignores the volume, and it
  says so: its level is labelled in dB.
- `lib/signal` now also holds `ScopeScale` and `Spectrum`, both ready
  for the analyzer.
- Serial gained `SWIPE x1 y1 x2 y2`, a dragged finger, next to `TAP` and
  `KNOB`. It is how the band's swipe was checked on the device.
- Not done: a frequency sweep, stereo channel selection.
