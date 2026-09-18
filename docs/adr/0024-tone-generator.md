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
| `TriggeredScope` | Sample window → a trace that stands still | recorder, analyzer |
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
triggers on a rising zero crossing with hysteresis, placed between samples
by interpolation, and spans two periods of the set frequency. Noise has
no period, so its timebase is fixed at 20 ms.

- **The ring grew from 1024 to 4096 samples** (+6 KB internal RAM) so
  low tones fit: two periods of 20 Hz need 4800 samples at 48 kHz, so
  4096 shows about 1.7. Measured after the change: 96 KB of internal RAM
  still free.
- **The trace is scaled to the set level.** The shape fills the band at
  −60 dB as well as at 0 dB; the number below it says how loud it is.
- **It reads through `SampleSource`, not the player.**
  `PlaybackStateMachine::readRecentSamples()` answers only while music
  plays, and the generator pauses music before it sounds, so the scope
  was first seen flat under a measured 1 kHz tone. `ToneOutput`
  implements `SampleSource` over the ring. The microphone will implement
  the same interface.
- At the top of the range the trace shows what 48 kHz really gives:
  about 2.4 points per period at 19.9 kHz. That is the signal as the DAC
  receives it, not a drawing fault.

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
- Not done: a frequency sweep, stereo channel selection, pink noise.
