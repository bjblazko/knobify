# Tone generator — design

*2026-09-18. Approved in a planning session with the user; decisions below
were each asked and answered.*

## Why

Three related tools are planned: a **tone generator**, a **voice recorder**
and a **spectrum analyzer** (microphone, or knobify's own output). What
they share is a signal path and a scope/spectrum display. Only the tone
generator is built now, but its pieces are cut so the other two reuse them:
an oscillator, a triggered scope, a scope widget, and a sample source (the
DAC ring now, the PDM microphone later).

## What the user gets

A seventh Home entry, **Tones**. The screen opens silent:

- **Waveforms:** Sine, Square (duty 5–95 %), Saw (symmetry 0–100 %:
  rising → triangle → falling), Noise (white).
- **Frequency** 20 Hz – 20 kHz on a 1/48-octave grid anchored at 1 kHz;
  turning faster takes bigger steps (up to 1/3 octave per detent).
- **Level** in dBFS, −60 … 0, 1 dB per detent, **independent of the device
  volume** — the number on screen is what leaves the jack. −20 dB on first
  use.
- **Output** at 48 kHz, both channels, 3.5 mm jack.
- **Display:** a real oscilloscope of the samples going to the DAC,
  triggered on the rising zero crossing so the picture stands still.
- **Music** that is playing is paused when the tone starts.
- **Persistence:** waveform, frequency, level, duty and symmetry survive a
  reboot. Leaving the screen, locking, or the sleep timer silence it.

## Interaction

The knob only rotates (ux-guidelines §4), so "mode buttons" are touch
chips. Top to bottom, all centred (never a corner, §7):

```
          ⌄                 back
        [ Saw ]             waveform chip (knob steps through waveforms)
   ╱|  ╱|  ╱|  ╱|           scope band, ~240×110
       440.0 Hz             the selected value, large
   ( Hz ) ( dB ) ( Shape )  parameter chips
          ▶                 start/stop, the screen's one primary button
```

- Tapping a chip selects it (`ink` fill); the selection stays, and the knob
  adjusts it. Selection happens on *press*, so hold-and-turn works too —
  the established two-handed gesture (§7, ADR 0013/0021).
- Chips appear only where they do something: Noise has no Hz chip; Sine
  and Noise have no shape chip; Square's is labelled **Duty**, Saw's
  **Shape**. Default selection is Hz (Level on Noise).
- The waveform chip is a chip like the others: selected, the knob steps
  Sine → Square → Saw → Noise (no wrap).

## Architecture

```
core 1 (loop / UI)                          core 0 (audio)
ScreenManagerToneGenerator ─┐
InputRouter ── turn ──▶ ToneSession ──▶ GeneratorControl (atomics)
                            │  ToneSettings         │
                            │  (pure)               ▼
                            │               ToneOutput task
                            │               Oscillator::render → 48 kHz
                            │               AudioOutputStage::writeFramesUnscaled
                            ▼                       │ ring (4096)
     TriggeredScope ◀── readRecentSamples ◀─────────┘
          ▼
     ui_widgets::ScopeTrace (lv_line)
```

### `lib/signal/` — new, pure, host-tested (namespace `knobify::signal`)

The shared home for all three tools. No Arduino, no LVGL.

- **`Waveform.h`**: `enum class Waveform : uint8_t { Sine, Square, Saw,
  Noise }` (persisted by value, append-only) and `OscillatorParams
  { waveform, frequencyHz, amplitude (linear 0..1), shape (0..1) }`.
- **`Oscillator.h`**: 32-bit phase accumulator; `render(int16_t *mono,
  size_t n, uint32_t rate)`. Sine from a 1024-entry table with linear
  interpolation. Square (duty) and Saw (symmetry) with PolyBLEP at their
  steps — at 48 kHz naive edges alias audibly from a few kHz. The saw's
  jump only exists at symmetry 0 or 1; in between it is a triangle-like
  shape with slope changes only, left naive. Noise from a 32-bit xorshift.
  Frequency and shape changes are phase-continuous; amplitude, start and
  stop ramp over ~5 ms so nothing clicks. `idle()` is true once a stop's
  ramp has finished.
- **`GeneratorControl.h`**: the core-1 → core-0 handover. One atomic per
  field (Hz in 1/100 Hz, amplitude and shape in Q15, waveform, running);
  a torn read between fields lasts one chunk (~2.7 ms) and is inaudible.
  Same single-producer/single-consumer, lock-free shape as the blip
  request in `playback::ToneGenerator`.
- **`ToneSettings.h`**: the model. Waveform, frequency grid index, level
  in dB, duty, symmetry, selected parameter. `turn(delta, nowMs)` with
  per-parameter acceleration from detent speed; clamping; which chips are
  visible for a waveform; `params()` → `OscillatorParams`; load/save
  through `playback::KeyValueStore` (U8 keys; the frequency index as two
  bytes).
- **`ToneSession.h`**: ties settings, running state, a
  `GeneratorOutput&` (tiny interface: `apply(params)`, `start()`,
  `stop()`) and the store together. `turn()` applies immediately;
  settings are saved ~2 s after the last change and on `stop()`/leaving,
  never per detent.
- **`TriggeredScope.h`**: samples + rate + pixel width → y values. Rising
  zero crossing with hysteresis; timebase ≈ 2 periods (the caller's
  frequency hint when it has one — the generator does — otherwise
  estimated from crossings), clamped to the window. Silence gives the
  zero line. This is what the recorder and analyzer will feed microphone
  samples into.

`playback::ToneGenerator` (the games' blips) is unchanged: another purpose,
its own tests.

### Drivers

- **`AudioOutputStage`**: new `writeFramesUnscaled()` — no volume, no
  output gain (dBFS is absolute), but it still records the ring. Ring
  1024 → 4096 samples (8 KB) so the scope sees two periods of 20 Hz at
  48 kHz. `readRecentSamples()` keeps clamping to its caller's
  `maxSamples`, so the Now Playing spectrum is unaffected.
- **`ToneOutput`** (the idle writer, ADR 0022) gains a second source and
  implements `signal::GeneratorOutput`: while the generator is running or
  ramping out it claims 48 kHz and writes `Oscillator` chunks through
  `writeFramesUnscaled()`. One task still owns the I2S port whenever no
  decoder does, and the existing rate-claim logic is reused. Blips and the
  generator never coincide (no generator inside a game).
- **Fix, `Esp32AudioI2SDriver::resume()`**: restore the track's own I2S
  rate. Today a paused track resumed after a game blip (22.05 kHz) or,
  now, a tone (48 kHz) plays at the wrong speed, because the library's
  `pauseResume()` does not re-set the rate.
- `-DKNOBIFY_GENERATOR_DEBUG`: logs the frequency and peak measured from
  the ring, and the worst chunk time, once a second.

### UI

- **`ui_widgets::ScopeTrace`**: thin `lv_line` wrapper (~160 points, `ink`
  on `surface`, a `surfaceAlt` zero line). Not a canvas — the same
  partial-invalidation argument as ADR 0022.
- **`ScreenManagerToneGenerator.cpp`** + `ScreenKind::ToneGenerator`
  (appended; resume records store kinds by value). Scope redrawn at
  ~30 fps while visible; stopped, it decays to the zero line.
- **Home**: an appended `kMenuEntries` row (append-only, it is the
  `menuVis` bit order), so Settings > Main menu can hide it. Icon: a
  Material Symbols waveform glyph added to `IconFont48.c` the documented
  way (same source font, `lv_font_conv`, `--no-compress`).
- **InputRouter**: on `ToneGenerator` the knob goes to `ToneSession::turn`
  (plain logic it can hold, like `BrightnessSetting`); `loop()` then calls
  a cheap `updateToneGeneratorDisplay()`.
- **Lifecycle**: ▶ pauses music (if playing) and starts; `render()`
  leaving the screen, the lock and the sleep timer's fade stop it.

## Docs

- `docs/adr/0024-tone-generator.md`, including the `lib/signal` layering
  for the recorder and analyzer.
- `docs/design/ux-guidelines.md` §5: Home list and encoder contexts.
- `README.md`: feature list; recorder and analyzer in the backlog.

## Tests (TDD, `pio test -e native`)

- `test_oscillator`: frequency by zero crossings (20 Hz, 1 kHz, 15 kHz at
  48 kHz); duty ratio; saw symmetry (rise vs fall time); peak = dBFS
  ±0.5 dB; noise mean ≈ 0 and within bounds; no jump on a frequency change;
  start/stop ramps; `idle()` after stop.
- `test_tone_settings`: grid (1 kHz exact, clamped 20 Hz/20 kHz),
  acceleration tiers, dB clamps, visible chips per waveform, selection
  normalisation, persistence round-trip, defaults.
- `test_tone_session`: turn applies to output; start/stop; debounced save.
- `test_scope`: finds the rising edge; stable across consecutive windows;
  timebase ≈ 2 periods; silence → zero line.
- `test_input`: the new knob route.

## Verification on the device

1. `scripts/check.sh` green.
2. Flash with `-DKNOBIFY_GENERATOR_DEBUG`; serial shows measured
   frequency/level matching 1 kHz/−20 dB, 20 Hz, 15 kHz.
3. Screenshots per waveform, plus a look at the physical round screen.
4. Listening: no clicks turning, starting, stopping; music pauses on ▶;
   back silences; values survive a reboot.
5. Regressions: game blips, Now Playing spectrum, a paused track resumed
   after the generator plays at the right speed.
