# Tone Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A "Tones" destination on Home that plays sine/square/saw/noise out of the 3.5 mm jack at a dBFS level, with frequency, level and wave shape set by the knob via touch chips, and a live oscilloscope of what actually goes to the DAC.

**Architecture:** Pure, host-tested signal code in a new `lib/signal/` (oscillator, triggered scope, settings model, session), reused later by the voice recorder and spectrum analyzer. The existing idle-writer task (`ToneOutput`, core 0) renders the oscillator at 48 kHz through a new unscaled write on `AudioOutputStage`; the UI reads the stage's sample ring back for the scope.

**Tech Stack:** C++17, PlatformIO (`esp32-s3` + `native` envs), Unity tests, LVGL 8, ESP-IDF I2S legacy driver.

**Spec:** `docs/superpowers/specs/2026-09-18-tone-generator-design.md`

## Global Constraints

- Frequency grid: 1/48 octave anchored at 1 kHz, steps −270 (20.26 Hz) … +207 (19.87 kHz).
- Level: integer dBFS −60 … 0, default −20; independent of the device volume and the sleep-timer output gain.
- Duty 5–95 % (default 50); saw symmetry 0–100 % where 0 = rising saw, 50 = triangle, 100 = falling saw (default 0).
- Output sample rate `signal::kGeneratorSampleRate = 48000`.
- NVS keys ≤ 15 chars: `tgWave`, `tgFreqHi`, `tgFreqLo`, `tgLevel`, `tgDuty`, `tgSym`.
- `ScreenKind` and `kMenuEntries` are **append-only** (stored by value / bit order).
- UI never uses a corner; everything top-/bottom-centred (ux-guidelines §7). Colours only from `lib/ui/Theme.h`.
- Home label: `Tones`. Waveform names: `Sine`, `Square`, `Saw`, `Noise`. Chip labels: `Hz`, `dB`, `Duty` (square) / `Shape` (saw).
- `lib/signal/` has no Arduino and no LVGL includes.
- Run host tests with `pio test -e native -f <test_dir>`; full gate is `scripts/check.sh`.
- Commit messages: plain imperative subject, a body explaining why, and the trailer `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File map

| File | Responsibility |
|---|---|
| `lib/signal/Waveform.h` (new) | `Waveform` enum, `OscillatorParams` |
| `lib/signal/Oscillator.h` (new) | Band-limited-enough waveform synthesis with click-free ramps |
| `lib/signal/TriggeredScope.h` (new) | Sample window → stable scope trace |
| `lib/signal/ToneSettings.h` (new) | The screen's model: values, knob stepping, chips, text, persistence |
| `lib/signal/GeneratorControl.h` (new) | `GeneratorOutput` interface, lock-free core1→core0 handover, sample rate |
| `lib/signal/ToneSession.h` (new) | Settings + running state + output + debounced save |
| `lib/navigation/ScreenId.h` | append `ScreenKind::ToneGenerator` |
| `lib/input/InputRouter.h` | route the knob to `ToneSession::turn` |
| `lib/drivers-audio/AudioOutputStage.{h,cpp}` | ring 4096, `writeFramesUnscaled()` |
| `lib/drivers-audio/ToneOutput.{h,cpp}` | render the generator when it runs |
| `lib/drivers-audio/Esp32AudioI2SDriver.h` | restore the track's I2S rate on resume |
| `lib/ui-widgets/ScopeTrace.h` (new) | `lv_line` scope trace |
| `lib/ui/ScreenManagerToneGenerator.cpp` (new) | the screen |
| `lib/ui/ScreenManager.{h,cpp}`, `ScreenManagerMenu.cpp` | wiring, Home entry, caption, reset |
| `lib/ui-widgets/IconFont48.c`, `IconFont.h` | the Home tile's glyph |
| `src/main.cpp` | globals, begin, loop hooks, lock/sleep stop |
| `test/test_oscillator`, `test_scope`, `test_tone_settings`, `test_tone_session` (new), `test/test_input` | tests |
| `docs/adr/0024-tone-generator.md` (new), `docs/adr/README.md`, `docs/design/ux-guidelines.md`, `README.md` | docs |

---

### Task 1: Oscillator

**Files:**
- Create: `lib/signal/Waveform.h`, `lib/signal/Oscillator.h`
- Test: `test/test_oscillator/test_oscillator.cpp`

**Interfaces:**
- Produces: `knobify::signal::Waveform { Sine, Square, Saw, Noise }`, `kWaveformCount = 4`, `struct OscillatorParams { Waveform waveform; float frequencyHz; float amplitude; float shape; }`, `class Oscillator { void setParams(const OscillatorParams&); void start(); void stop(); bool idle() const; void render(int16_t *out, size_t count, uint32_t sampleRate); static constexpr uint32_t kRampMs = 5; }`

- [ ] **Step 1: Write `Waveform.h`** (a plain type header, needed by the test to compile)

```cpp
#pragma once

#include <cstdint>

namespace knobify::signal {

// What the tone generator can play (ADR 0024). Stored in NVS by value, so
// append-only.
enum class Waveform : uint8_t { Sine = 0, Square = 1, Saw = 2, Noise = 3 };
constexpr uint8_t kWaveformCount = 4;

// Everything an Oscillator needs to know, already in its own units: the
// knob-facing units (grid steps, dB, percent) live in ToneSettings.
struct OscillatorParams {
  Waveform waveform = Waveform::Sine;
  float frequencyHz = 1000.0f;
  // Linear, 0..1 of full scale.
  float amplitude = 0.1f;
  // Square: the duty cycle, 0..1. Saw: the fraction of the period spent
  // rising -- 1 is a rising saw, 0.5 a triangle, 0 a falling saw. Unused
  // by Sine and Noise.
  float shape = 0.5f;
};

}  // namespace knobify::signal
```

- [ ] **Step 2: Write the failing test** `test/test_oscillator/test_oscillator.cpp`

```cpp
#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>

#include "Oscillator.h"

using knobify::signal::Oscillator;
using knobify::signal::OscillatorParams;
using knobify::signal::Waveform;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;

OscillatorParams params(Waveform wave, float hz, float amplitude = 1.0f,
                        float shape = 0.5f) {
  OscillatorParams p;
  p.waveform = wave;
  p.frequencyHz = hz;
  p.amplitude = amplitude;
  p.shape = shape;
  return p;
}

// Starts the oscillator and renders past the start ramp, so what follows
// is the steady state.
std::vector<int16_t> steady(Oscillator &osc, size_t count) {
  osc.start();
  std::vector<int16_t> ramp(kRate / 100);  // 10 ms, twice the ramp.
  osc.render(ramp.data(), ramp.size(), kRate);
  std::vector<int16_t> out(count);
  osc.render(out.data(), out.size(), kRate);
  return out;
}

int risingCrossings(const std::vector<int16_t> &s) {
  int n = 0;
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i - 1] < 0 && s[i] >= 0) ++n;
  }
  return n;
}
}  // namespace

void test_a_stopped_oscillator_is_silent_and_idle() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000));
  std::vector<int16_t> out(480, 123);
  osc.render(out.data(), out.size(), kRate);
  TEST_ASSERT_TRUE(osc.idle());
  for (int16_t s : out) TEST_ASSERT_EQUAL_INT16(0, s);
}

void test_a_sine_is_at_the_asked_for_pitch() {
  const float pitches[] = {20.0f, 1000.0f, 15000.0f};
  const int expected[] = {20, 1000, 15000};
  for (int i = 0; i < 3; ++i) {
    Oscillator osc;
    osc.setParams(params(Waveform::Sine, pitches[i]));
    TEST_ASSERT_INT_WITHIN(2, expected[i], risingCrossings(steady(osc, kRate)));
  }
}

void test_the_level_is_the_asked_for_amplitude() {
  Oscillator osc;
  osc.setParams(params(Waveform::Sine, 1000, 0.1f));  // -20 dBFS.
  auto s = steady(osc, 480);
  int peak = 0;
  for (int16_t v : s) peak = std::max(peak, std::abs(static_cast<int>(v)));
  // +-0.5 dB around 3277.
  TEST_ASSERT_INT_WITHIN(190, 3277, peak);
}

void test_a_square_honours_its_duty_cycle() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000, 1.0f, 0.25f));
  auto s = steady(osc, kRate);
  int high = 0;
  for (int16_t v : s) high += v > 0;
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, static_cast<float>(high) / s.size());
}

void test_a_saw_rises_falls_or_does_both_by_its_shape() {
  const float shapes[] = {1.0f, 0.5f, 0.0f};
  const float risingShare[] = {0.95f, 0.5f, 0.05f};
  for (int i = 0; i < 3; ++i) {
    Oscillator osc;
    osc.setParams(params(Waveform::Saw, 1000, 1.0f, shapes[i]));
    auto s = steady(osc, kRate);
    int rising = 0;
    for (size_t k = 1; k < s.size(); ++k) rising += s[k] > s[k - 1];
    TEST_ASSERT_FLOAT_WITHIN(0.06f, risingShare[i],
                             static_cast<float>(rising) / (s.size() - 1));
  }
}

void test_noise_is_centred_bounded_and_not_a_pattern() {
  Oscillator osc;
  osc.setParams(params(Waveform::Noise, 1000, 0.5f));
  auto s = steady(osc, kRate);
  long long sum = 0;
  std::set<int16_t> distinct;
  for (int16_t v : s) {
    sum += v;
    distinct.insert(v);
    TEST_ASSERT_TRUE(std::abs(static_cast<int>(v)) <= 16384);
  }
  TEST_ASSERT_TRUE(std::llabs(sum / static_cast<long long>(s.size())) < 300);
  TEST_ASSERT_TRUE(distinct.size() > 1000);
}

void test_a_pitch_change_does_not_jump() {
  Oscillator osc;
  osc.setParams(params(Waveform::Sine, 1000));
  auto before = steady(osc, 100);
  osc.setParams(params(Waveform::Sine, 1500));
  std::vector<int16_t> after(100);
  osc.render(after.data(), after.size(), kRate);
  // The largest step a 1.5 kHz full-scale sine takes between samples,
  // with some room: a restarted phase would jump by up to 2x full scale.
  const int maxStep = 7100;
  TEST_ASSERT_TRUE(std::abs(after[0] - before.back()) < maxStep);
  for (size_t i = 1; i < after.size(); ++i) {
    TEST_ASSERT_TRUE(std::abs(after[i] - after[i - 1]) < maxStep);
  }
}

void test_start_and_stop_ramp_instead_of_clicking() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000, 1.0f));
  osc.start();
  std::vector<int16_t> start(kRate / 1000);  // 1 ms.
  osc.render(start.data(), start.size(), kRate);
  TEST_ASSERT_TRUE(std::abs(start[0]) < 400);
  TEST_ASSERT_TRUE(std::abs(start.back()) < 32767 / 2);

  osc.stop();
  TEST_ASSERT_FALSE(osc.idle());
  std::vector<int16_t> tail(kRate / 100);  // 10 ms.
  osc.render(tail.data(), tail.size(), kRate);
  TEST_ASSERT_TRUE(osc.idle());
  TEST_ASSERT_EQUAL_INT16(0, tail.back());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_stopped_oscillator_is_silent_and_idle);
  RUN_TEST(test_a_sine_is_at_the_asked_for_pitch);
  RUN_TEST(test_the_level_is_the_asked_for_amplitude);
  RUN_TEST(test_a_square_honours_its_duty_cycle);
  RUN_TEST(test_a_saw_rises_falls_or_does_both_by_its_shape);
  RUN_TEST(test_noise_is_centred_bounded_and_not_a_pattern);
  RUN_TEST(test_a_pitch_change_does_not_jump);
  RUN_TEST(test_start_and_stop_ramp_instead_of_clicking);
  return UNITY_END();
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `pio test -e native -f test_oscillator`
Expected: FAIL — `Oscillator.h: No such file or directory`.

- [ ] **Step 4: Write `lib/signal/Oscillator.h`**

```cpp
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "Waveform.h"

namespace knobify::signal {

// The tone generator's voice (ADR 0024). Pure logic: the audio task calls
// render() with a buffer and a rate, and nothing here knows about I2S.
//
// A 32-bit phase accumulator, so a pitch change is phase-continuous. The
// square's and the saw's jumps are smoothed with PolyBLEP: at 48 kHz a
// naive edge aliases audibly from a few kHz up, and PolyBLEP costs two
// multiplies near each edge. The saw's in-between shapes have no jump,
// only corners, and stay naive.
//
// The level ramps over kRampMs whenever it changes, starts or stops -- a
// step in level is a click, and a tool for testing speakers must not
// produce its own.
class Oscillator {
 public:
  static constexpr uint32_t kRampMs = 5;

  Oscillator() {
    for (size_t i = 0; i <= kSineTableSize; ++i) {
      sine_[i] = static_cast<float>(
          std::sin(kTwoPi * static_cast<double>(i) / kSineTableSize));
    }
  }

  void setParams(const OscillatorParams &params) { params_ = params; }
  void start() { running_ = true; }
  void stop() { running_ = false; }
  // True once stopped *and* the fade-out has reached silence.
  bool idle() const { return !running_ && gain_ <= 0.0f; }

  void render(int16_t *out, size_t count, uint32_t sampleRate) {
    if (sampleRate == 0) {
      std::fill(out, out + count, 0);
      return;
    }
    double cycles = static_cast<double>(params_.frequencyHz) / sampleRate;
    cycles = std::clamp(cycles, 0.0, 0.5);
    const auto increment = static_cast<uint32_t>(cycles * 4294967296.0);
    const auto dt = static_cast<float>(cycles);
    const float target =
        running_ ? std::clamp(params_.amplitude, 0.0f, 1.0f) : 0.0f;
    const float rampStep =
        1000.0f / (static_cast<float>(sampleRate) * static_cast<float>(kRampMs));

    for (size_t i = 0; i < count; ++i) {
      if (gain_ < target) {
        gain_ = std::min(target, gain_ + rampStep);
      } else if (gain_ > target) {
        gain_ = std::max(target, gain_ - rampStep);
      }
      if (gain_ <= 0.0f) {
        out[i] = 0;
        continue;
      }
      const float value = valueAt(dt);
      phase_ += increment;
      const long scaled = std::lround(value * gain_ * 32767.0f);
      out[i] = static_cast<int16_t>(std::clamp(scaled, -32767L, 32767L));
    }
  }

 private:
  static constexpr double kTwoPi = 6.283185307179586;
  static constexpr uint32_t kSineBits = 10;
  static constexpr size_t kSineTableSize = size_t{1} << kSineBits;
  static constexpr uint32_t kFracBits = 32 - kSineBits;
  static constexpr float kPhaseToUnit = 1.0f / 4294967296.0f;

  float valueAt(float dt) {
    const float t = static_cast<float>(phase_) * kPhaseToUnit;
    switch (params_.waveform) {
      case Waveform::Sine:
        return sine();
      case Waveform::Square: {
        const float duty = std::clamp(params_.shape, 0.01f, 0.99f);
        float v = t < duty ? 1.0f : -1.0f;
        v += polyBlep(t, dt);
        float sinceFall = t - duty;
        if (sinceFall < 0.0f) sinceFall += 1.0f;
        v -= polyBlep(sinceFall, dt);
        return v;
      }
      case Waveform::Saw: {
        const float rise = std::clamp(params_.shape, 0.0f, 1.0f);
        if (rise >= 1.0f) return 2.0f * t - 1.0f - polyBlep(t, dt);
        if (rise <= 0.0f) return 1.0f - 2.0f * t + polyBlep(t, dt);
        return t < rise ? -1.0f + 2.0f * t / rise
                        : 1.0f - 2.0f * (t - rise) / (1.0f - rise);
      }
      case Waveform::Noise:
        // xorshift32: white enough to test a speaker with, and cheap.
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return static_cast<float>(static_cast<int32_t>(noise_)) *
               (1.0f / 2147483648.0f);
    }
    return 0.0f;
  }

  float sine() const {
    const uint32_t index = phase_ >> kFracBits;
    const float frac = static_cast<float>(phase_ & ((1u << kFracBits) - 1)) *
                       (1.0f / static_cast<float>(1u << kFracBits));
    return sine_[index] + (sine_[index + 1] - sine_[index]) * frac;
  }

  // The correction for a unit step from -1 to +1 at t == 0, spread over
  // the sample either side of it.
  static float polyBlep(float t, float dt) {
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) {
      t /= dt;
      return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
      t = (t - 1.0f) / dt;
      return t * t + t + t + 1.0f;
    }
    return 0.0f;
  }

  std::array<float, kSineTableSize + 1> sine_{};
  OscillatorParams params_;
  uint32_t phase_ = 0;
  uint32_t noise_ = 0x9E3779B9u;
  float gain_ = 0.0f;
  bool running_ = false;
};

}  // namespace knobify::signal
```

- [ ] **Step 5: Run it to verify it passes**

Run: `pio test -e native -f test_oscillator`
Expected: 8 tests PASS. If the saw-share test misses by a hair, check the blep sign first (a falling saw must *add* the blep), not the tolerance.

- [ ] **Step 6: Commit**

```bash
git add lib/signal/Waveform.h lib/signal/Oscillator.h test/test_oscillator
git commit -m "Add the tone generator's oscillator" -m "Sine, square with duty, saw with symmetry, and noise, rendered into a buffer at any rate. First piece of lib/signal, the pure layer the recorder and analyzer will share (ADR 0024)." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Triggered scope

**Files:**
- Create: `lib/signal/TriggeredScope.h`
- Test: `test/test_scope/test_scope.cpp`

**Interfaces:**
- Produces: `knobify::signal::TriggeredScope::trace(const int16_t *samples, size_t count, uint32_t sampleRate, float frequencyHintHz, int16_t *out, size_t width)` (static; `frequencyHintHz <= 0` = estimate), `TriggeredScope::kPeriods = 2`.

- [ ] **Step 1: Write the failing test** `test/test_scope/test_scope.cpp`

```cpp
#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "TriggeredScope.h"

using knobify::signal::TriggeredScope;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;
constexpr double kPi = 3.141592653589793;
constexpr size_t kWindow = 4096;
constexpr size_t kWidth = 160;

std::vector<int16_t> sine(float hz, float amplitude, size_t offset = 0) {
  std::vector<int16_t> s(kWindow);
  for (size_t i = 0; i < kWindow; ++i) {
    s[i] = static_cast<int16_t>(std::lround(
        amplitude * std::sin(2.0 * kPi * hz * (i + offset) / kRate)));
  }
  return s;
}

std::vector<int16_t> trace(const std::vector<int16_t> &s, float hint) {
  std::vector<int16_t> out(kWidth);
  TriggeredScope::trace(s.data(), s.size(), kRate, hint, out.data(), kWidth);
  return out;
}
}  // namespace

void test_a_sine_starts_on_its_rising_zero_crossing() {
  auto out = trace(sine(1000, 10000, 13), 1000);
  // Two periods across 160 points: one period is ~80 points.
  TEST_ASSERT_TRUE(std::abs(out[0]) < 400);
  TEST_ASSERT_TRUE(out[20] > 9500);
  TEST_ASSERT_TRUE(out[60] < -9500);
}

void test_the_picture_stands_still_between_windows() {
  auto a = trace(sine(1000, 10000, 0), 1000);
  auto b = trace(sine(1000, 10000, 37), 1000);
  for (size_t i = 0; i < kWidth; ++i) TEST_ASSERT_INT_WITHIN(250, a[i], b[i]);
}

void test_without_a_hint_the_period_is_estimated() {
  auto out = trace(sine(1000, 10000, 13), 0.0f);
  TEST_ASSERT_TRUE(std::abs(out[0]) < 400);
  TEST_ASSERT_TRUE(out[20] > 9000);
}

void test_silence_is_the_zero_line() {
  std::vector<int16_t> silent(kWindow, 0);
  for (int16_t v : trace(silent, 1000)) TEST_ASSERT_EQUAL_INT16(0, v);
}

void test_a_period_longer_than_the_window_still_draws() {
  auto out = trace(sine(20, 10000), 20);
  bool moved = false;
  for (int16_t v : out) {
    TEST_ASSERT_TRUE(std::abs(v) <= 10000);
    moved |= v != out[0];
  }
  TEST_ASSERT_TRUE(moved);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_sine_starts_on_its_rising_zero_crossing);
  RUN_TEST(test_the_picture_stands_still_between_windows);
  RUN_TEST(test_without_a_hint_the_period_is_estimated);
  RUN_TEST(test_silence_is_the_zero_line);
  RUN_TEST(test_a_period_longer_than_the_window_still_draws);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f test_scope`
Expected: FAIL — `TriggeredScope.h: No such file or directory`.

- [ ] **Step 3: Write `lib/signal/TriggeredScope.h`**

```cpp
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace knobify::signal {

// Turns a window of recent samples into an oscilloscope trace (ADR 0024):
// `width` values, each a sample value, starting on a rising zero crossing
// so a periodic signal draws the same picture frame after frame.
//
// The trigger needs the signal to have been below -hysteresis before the
// crossing counts, so noise riding on a slow wave cannot fire it early.
// The crossing is placed between samples by interpolation: at high
// pitches a whole-sample trigger makes the picture shimmer.
//
// Pure logic. It knows nothing about where the samples came from -- the
// DAC's ring now, the microphone for the recorder and analyzer later.
class TriggeredScope {
 public:
  static constexpr float kPeriods = 2.0f;

  // frequencyHintHz <= 0: estimate the period from the zero crossings.
  static void trace(const int16_t *samples, size_t count, uint32_t sampleRate,
                    float frequencyHintHz, int16_t *out, size_t width) {
    if (width == 0) return;
    const int peak = count >= 2 && sampleRate != 0 ? peakOf(samples, count) : 0;
    if (peak == 0) {
      std::fill(out, out + width, 0);
      return;
    }
    const int hysteresis = std::max(peak / 8, 16);
    const float last = static_cast<float>(count - 1);

    float hz = frequencyHintHz;
    if (hz <= 0.0f) hz = estimateHz(samples, count, sampleRate, hysteresis);
    float span = hz > 0.0f ? kPeriods * static_cast<float>(sampleRate) / hz : last;
    span = std::clamp(span, 2.0f, last);

    float start = latestTrigger(samples, count, span, hysteresis);
    if (start < 0.0f) start = last - span;

    for (size_t i = 0; i < width; ++i) {
      const float pos =
          width == 1 ? start
                     : start + span * static_cast<float>(i) /
                                   static_cast<float>(width - 1);
      const auto index = std::min(static_cast<size_t>(pos), count - 2);
      const float frac = pos - static_cast<float>(index);
      const float v = samples[index] + (samples[index + 1] - samples[index]) * frac;
      out[i] = static_cast<int16_t>(std::lround(v));
    }
  }

 private:
  static int peakOf(const int16_t *s, size_t count) {
    int peak = 0;
    for (size_t i = 0; i < count; ++i) peak = std::max(peak, std::abs(static_cast<int>(s[i])));
    return peak;
  }

  // Calls `found(position)` for every armed rising crossing, in order.
  template <typename F>
  static void forEachCrossing(const int16_t *s, size_t count, int hysteresis,
                              F found) {
    bool armed = false;
    for (size_t i = 1; i < count; ++i) {
      if (s[i] < -hysteresis) armed = true;
      if (armed && s[i - 1] < 0 && s[i] >= 0) {
        const float fraction = static_cast<float>(-s[i - 1]) /
                               static_cast<float>(s[i] - s[i - 1]);
        found(static_cast<float>(i - 1) + fraction);
        armed = false;
      }
    }
  }

  // The newest crossing that still leaves `span` samples after it.
  static float latestTrigger(const int16_t *s, size_t count, float span,
                             int hysteresis) {
    const float last = static_cast<float>(count - 1);
    float best = -1.0f;
    forEachCrossing(s, count, hysteresis, [&](float at) {
      if (at + span <= last) best = at;
    });
    return best;
  }

  static float estimateHz(const int16_t *s, size_t count, uint32_t sampleRate,
                          int hysteresis) {
    float first = -1.0f;
    float lastSeen = -1.0f;
    int crossings = 0;
    forEachCrossing(s, count, hysteresis, [&](float at) {
      if (first < 0.0f) first = at;
      lastSeen = at;
      ++crossings;
    });
    if (crossings < 2) return 0.0f;
    const float period = (lastSeen - first) / static_cast<float>(crossings - 1);
    return period > 0.0f ? static_cast<float>(sampleRate) / period : 0.0f;
  }
};

}  // namespace knobify::signal
```

- [ ] **Step 4: Run it to verify it passes**

Run: `pio test -e native -f test_scope`
Expected: 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/signal/TriggeredScope.h test/test_scope
git commit -m "Add a triggered scope for sample windows" -m "Starts every trace on a rising zero crossing with hysteresis, interpolated between samples, so a periodic signal stands still. It takes plain samples so the recorder and analyzer can feed it the microphone later (ADR 0024)." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Tone settings

**Files:**
- Create: `lib/signal/ToneSettings.h`
- Test: `test/test_tone_settings/test_tone_settings.cpp`

**Interfaces:**
- Consumes: `Waveform`, `OscillatorParams` (Task 1); `knobify::playback::KeyValueStore` (`lib/playback/KeyValueStore.h`: `bool getU8(const std::string&, uint8_t&)`, `void setU8(const std::string&, uint8_t)`).
- Produces: `enum class ToneParam : uint8_t { Waveform, Frequency, Level, Shape }`, `kToneParamCount = 4`, and `class ToneSettings` with: `waveform()`, `frequencyStep()`, `frequencyHz()`, `levelDb()`, `dutyPercent()`, `symmetryPercent()`, `selected()`, `select(ToneParam)`, `bool turn(int delta, uint32_t nowMs)`, `OscillatorParams params() const`, `static bool visible(ToneParam, Waveform)`, `static float dbToLinear(int)`, `static const char *waveformName(Waveform)`, `static const char *chipLabel(ToneParam, Waveform)`, `void valueText(ToneParam, char *out, size_t size) const`, `void load(playback::KeyValueStore&)`, `void save(playback::KeyValueStore&) const`.

- [ ] **Step 1: Write the failing test** `test/test_tone_settings/test_tone_settings.cpp`

```cpp
#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "ToneSettings.h"

using knobify::playback::KeyValueStore;
using knobify::signal::ToneParam;
using knobify::signal::ToneSettings;
using knobify::signal::Waveform;

void setUp() {}
void tearDown() {}

namespace {
class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override { values[key] = value; }
  std::map<std::string, uint8_t> values;
};

// Slow detents: far enough apart that no acceleration applies.
void turnSlowly(ToneSettings &s, int detents, uint32_t &now) {
  const int step = detents > 0 ? 1 : -1;
  for (int i = 0; i != detents; i += step) {
    now += 1000;
    s.turn(step, now);
  }
}

std::string text(const ToneSettings &s, ToneParam p) {
  char buf[24];
  s.valueText(p, buf, sizeof(buf));
  return buf;
}
}  // namespace

void test_defaults() {
  ToneSettings s;
  TEST_ASSERT_EQUAL(Waveform::Sine, s.waveform());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, s.frequencyHz());
  TEST_ASSERT_EQUAL_INT(-20, s.levelDb());
  TEST_ASSERT_EQUAL_INT(50, s.dutyPercent());
  TEST_ASSERT_EQUAL_INT(0, s.symmetryPercent());
  TEST_ASSERT_EQUAL(ToneParam::Frequency, s.selected());
}

void test_frequency_moves_on_a_log_grid_and_clamps() {
  ToneSettings s;
  uint32_t now = 0;
  turnSlowly(s, 48, now);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, s.frequencyHz());
  turnSlowly(s, 400, now);
  TEST_ASSERT_EQUAL_INT(ToneSettings::kMaxFrequencyStep, s.frequencyStep());
  TEST_ASSERT_TRUE(s.frequencyHz() <= 20000.0f);
  turnSlowly(s, -600, now);
  TEST_ASSERT_EQUAL_INT(ToneSettings::kMinFrequencyStep, s.frequencyStep());
  TEST_ASSERT_TRUE(s.frequencyHz() >= 20.0f);
}

void test_turning_faster_takes_bigger_steps() {
  ToneSettings s;
  s.turn(1, 1000);  // First turn: slow.
  TEST_ASSERT_EQUAL_INT(1, s.frequencyStep());
  s.turn(1, 1300);  // 300 ms later: slow.
  TEST_ASSERT_EQUAL_INT(2, s.frequencyStep());
  s.turn(1, 1380);  // 80 ms: 12.5 detents/s, mid.
  TEST_ASSERT_EQUAL_INT(6, s.frequencyStep());
  s.turn(1, 1400);  // 20 ms: 50 detents/s, fast.
  TEST_ASSERT_EQUAL_INT(22, s.frequencyStep());
}

void test_level_is_whole_decibels_within_range() {
  ToneSettings s;
  s.select(ToneParam::Level);
  uint32_t now = 0;
  turnSlowly(s, 5, now);
  TEST_ASSERT_EQUAL_INT(-15, s.levelDb());
  turnSlowly(s, 30, now);
  TEST_ASSERT_EQUAL_INT(0, s.levelDb());
  turnSlowly(s, -80, now);
  TEST_ASSERT_EQUAL_INT(-60, s.levelDb());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, ToneSettings::dbToLinear(-60));
}

void test_the_shape_chip_follows_the_waveform() {
  ToneSettings s;
  TEST_ASSERT_FALSE(ToneSettings::visible(ToneParam::Shape, Waveform::Sine));
  TEST_ASSERT_TRUE(ToneSettings::visible(ToneParam::Shape, Waveform::Square));
  TEST_ASSERT_TRUE(ToneSettings::visible(ToneParam::Shape, Waveform::Saw));
  TEST_ASSERT_FALSE(ToneSettings::visible(ToneParam::Frequency, Waveform::Noise));
  TEST_ASSERT_EQUAL_STRING("Duty", ToneSettings::chipLabel(ToneParam::Shape, Waveform::Square));
  TEST_ASSERT_EQUAL_STRING("Shape", ToneSettings::chipLabel(ToneParam::Shape, Waveform::Saw));
  TEST_ASSERT_EQUAL_STRING("Noise", ToneSettings::chipLabel(ToneParam::Waveform, Waveform::Noise));

  s.select(ToneParam::Shape);  // Not on a sine: ignored.
  TEST_ASSERT_EQUAL(ToneParam::Frequency, s.selected());
}

void test_the_waveform_steps_without_wrapping_and_selection_follows() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 5, now);
  TEST_ASSERT_EQUAL(Waveform::Noise, s.waveform());
  s.select(ToneParam::Frequency);  // Noise has no pitch: ignored.
  TEST_ASSERT_EQUAL(ToneParam::Waveform, s.selected());
  turnSlowly(s, -1, now);
  TEST_ASSERT_EQUAL(Waveform::Saw, s.waveform());
}

void test_duty_and_symmetry_clamp_and_map_to_the_oscillator() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 1, now);  // Square.
  s.select(ToneParam::Shape);
  turnSlowly(s, 60, now);
  TEST_ASSERT_EQUAL_INT(95, s.dutyPercent());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, s.params().shape);

  s.select(ToneParam::Waveform);
  turnSlowly(s, 1, now);  // Saw.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.params().shape);  // Rising.
  s.select(ToneParam::Shape);
  turnSlowly(s, 50, now);
  TEST_ASSERT_EQUAL_INT(50, s.symmetryPercent());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, s.params().shape);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, s.params().amplitude);
}

void test_value_text() {
  ToneSettings s;
  TEST_ASSERT_EQUAL_STRING("1.00 kHz", text(s, ToneParam::Frequency).c_str());
  TEST_ASSERT_EQUAL_STRING("-20 dB", text(s, ToneParam::Level).c_str());
  TEST_ASSERT_EQUAL_STRING("Sine", text(s, ToneParam::Waveform).c_str());
  uint32_t now = 0;
  turnSlowly(s, -57, now);  // The grid step nearest A4: 439.0 Hz.
  TEST_ASSERT_EQUAL_STRING("439 Hz", text(s, ToneParam::Frequency).c_str());
  turnSlowly(s, -600, now);
  TEST_ASSERT_EQUAL_STRING("20.3 Hz", text(s, ToneParam::Frequency).c_str());
  turnSlowly(s, 600, now);
  TEST_ASSERT_EQUAL_STRING("19.9 kHz", text(s, ToneParam::Frequency).c_str());

  s.select(ToneParam::Waveform);
  turnSlowly(s, 2, now);  // Saw, symmetry 0.
  TEST_ASSERT_EQUAL_STRING("Rising", text(s, ToneParam::Shape).c_str());
  s.select(ToneParam::Shape);
  turnSlowly(s, 50, now);
  TEST_ASSERT_EQUAL_STRING("Triangle", text(s, ToneParam::Shape).c_str());
  turnSlowly(s, -15, now);
  TEST_ASSERT_EQUAL_STRING("Shape 35%", text(s, ToneParam::Shape).c_str());
}

void test_settings_survive_a_round_trip_and_garbage_is_ignored() {
  FakeStore store;
  ToneSettings a;
  uint32_t now = 0;
  a.select(ToneParam::Waveform);
  turnSlowly(a, 2, now);  // Saw.
  a.select(ToneParam::Frequency);
  turnSlowly(a, -100, now);
  a.select(ToneParam::Level);
  turnSlowly(a, -7, now);
  a.save(store);

  ToneSettings b;
  b.load(store);
  TEST_ASSERT_EQUAL(Waveform::Saw, b.waveform());
  TEST_ASSERT_EQUAL_INT(-100, b.frequencyStep());
  TEST_ASSERT_EQUAL_INT(-27, b.levelDb());

  store.values[ToneSettings::kWaveKey] = 9;
  store.values[ToneSettings::kLevelKey] = 200;
  ToneSettings c;
  c.load(store);
  TEST_ASSERT_EQUAL(Waveform::Sine, c.waveform());
  TEST_ASSERT_EQUAL_INT(-20, c.levelDb());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_frequency_moves_on_a_log_grid_and_clamps);
  RUN_TEST(test_turning_faster_takes_bigger_steps);
  RUN_TEST(test_level_is_whole_decibels_within_range);
  RUN_TEST(test_the_shape_chip_follows_the_waveform);
  RUN_TEST(test_the_waveform_steps_without_wrapping_and_selection_follows);
  RUN_TEST(test_duty_and_symmetry_clamp_and_map_to_the_oscillator);
  RUN_TEST(test_value_text);
  RUN_TEST(test_settings_survive_a_round_trip_and_garbage_is_ignored);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f test_tone_settings`
Expected: FAIL — `ToneSettings.h: No such file or directory`.

- [ ] **Step 3: Write `lib/signal/ToneSettings.h`**

```cpp
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "KeyValueStore.h"
#include "Waveform.h"

namespace knobify::signal {

// What the knob can be set to adjust on the tone generator (ADR 0024) --
// one touch chip each.
enum class ToneParam : uint8_t { Waveform, Frequency, Level, Shape };
constexpr int kToneParamCount = 4;

// The tone generator screen's model: the values in the units a person
// turns (grid steps, whole dB, percent), which chip the knob is on, how
// fast turning moves it, what each value reads as, and persistence.
// Pure logic over an explicit clock, like BrightnessSetting.
class ToneSettings {
 public:
  // 1/48 octave per slow detent, anchored at 1 kHz so the default is
  // exact. The ends are the last steps inside 20 Hz .. 20 kHz.
  static constexpr int kStepsPerOctave = 48;
  static constexpr int kMinFrequencyStep = -270;  // 20.26 Hz
  static constexpr int kMaxFrequencyStep = 207;   // 19.87 kHz
  static constexpr int kMinLevelDb = -60;
  static constexpr int kMaxLevelDb = 0;
  // Loud enough to hear, quiet enough for headphones on first use.
  static constexpr int kDefaultLevelDb = -20;
  static constexpr int kMinDutyPercent = 5;
  static constexpr int kMaxDutyPercent = 95;

  // Knob speed tiers, in detents per second. A gap longer than
  // kSpeedWindowMs always counts as slow. Tuned on the device.
  static constexpr uint32_t kMidDetentsPerSecond = 10;
  static constexpr uint32_t kFastDetentsPerSecond = 25;
  static constexpr uint32_t kSpeedWindowMs = 250;

  static constexpr char kWaveKey[] = "tgWave";
  static constexpr char kFreqHiKey[] = "tgFreqHi";
  static constexpr char kFreqLoKey[] = "tgFreqLo";
  static constexpr char kLevelKey[] = "tgLevel";
  static constexpr char kDutyKey[] = "tgDuty";
  static constexpr char kSymmetryKey[] = "tgSym";

  Waveform waveform() const { return waveform_; }
  int frequencyStep() const { return frequencyStep_; }
  float frequencyHz() const {
    return 1000.0f * std::exp2(static_cast<float>(frequencyStep_) / kStepsPerOctave);
  }
  int levelDb() const { return levelDb_; }
  int dutyPercent() const { return dutyPercent_; }
  // 0 = rising saw, 50 = triangle, 100 = falling saw.
  int symmetryPercent() const { return symmetryPercent_; }

  // A chip is shown only where it does something (ux-guidelines §7).
  static bool visible(ToneParam param, Waveform wave) {
    switch (param) {
      case ToneParam::Frequency:
        return wave != Waveform::Noise;
      case ToneParam::Shape:
        return wave == Waveform::Square || wave == Waveform::Saw;
      default:
        return true;
    }
  }

  // Never a chip that is not shown: Level, which every waveform has, stands
  // in for one that is not.
  ToneParam selected() const {
    return visible(selected_, waveform_) ? selected_ : ToneParam::Level;
  }
  void select(ToneParam param) {
    if (visible(param, waveform_)) selected_ = param;
  }

  // Adjusts the selected value; returns whether anything changed.
  bool turn(int delta, uint32_t nowMs) {
    if (delta == 0) return false;
    const ToneParam param = selected();
    const int step = delta * multiplier(param, speedTier(delta, nowMs));
    switch (param) {
      case ToneParam::Waveform: {
        const int next = std::clamp(static_cast<int>(waveform_) + delta, 0,
                                    kWaveformCount - 1);
        return set(waveform_, static_cast<Waveform>(next));
      }
      case ToneParam::Frequency:
        return set(frequencyStep_, std::clamp(frequencyStep_ + step,
                                              kMinFrequencyStep, kMaxFrequencyStep));
      case ToneParam::Level:
        return set(levelDb_, std::clamp(levelDb_ + step, kMinLevelDb, kMaxLevelDb));
      case ToneParam::Shape:
        if (waveform_ == Waveform::Square) {
          return set(dutyPercent_, std::clamp(dutyPercent_ + step, kMinDutyPercent,
                                              kMaxDutyPercent));
        }
        return set(symmetryPercent_, std::clamp(symmetryPercent_ + step, 0, 100));
    }
    return false;
  }

  OscillatorParams params() const {
    OscillatorParams p;
    p.waveform = waveform_;
    p.frequencyHz = frequencyHz();
    p.amplitude = dbToLinear(levelDb_);
    if (waveform_ == Waveform::Square) {
      p.shape = dutyPercent_ / 100.0f;
    } else if (waveform_ == Waveform::Saw) {
      // The oscillator wants the share of the period spent rising.
      p.shape = 1.0f - symmetryPercent_ / 100.0f;
    }
    return p;
  }

  static float dbToLinear(int db) {
    return std::pow(10.0f, static_cast<float>(db) / 20.0f);
  }

  static const char *waveformName(Waveform wave) {
    switch (wave) {
      case Waveform::Sine: return "Sine";
      case Waveform::Square: return "Square";
      case Waveform::Saw: return "Saw";
      case Waveform::Noise: return "Noise";
    }
    return "";
  }

  static const char *chipLabel(ToneParam param, Waveform wave) {
    switch (param) {
      case ToneParam::Waveform: return waveformName(wave);
      case ToneParam::Frequency: return "Hz";
      case ToneParam::Level: return "dB";
      case ToneParam::Shape: return wave == Waveform::Square ? "Duty" : "Shape";
    }
    return "";
  }

  void valueText(ToneParam param, char *out, size_t size) const {
    switch (param) {
      case ToneParam::Waveform:
        snprintf(out, size, "%s", waveformName(waveform_));
        return;
      case ToneParam::Frequency: {
        const float hz = frequencyHz();
        if (hz < 100.0f) {
          snprintf(out, size, "%.1f Hz", hz);
        } else if (hz < 1000.0f) {
          snprintf(out, size, "%.0f Hz", hz);
        } else if (hz < 10000.0f) {
          snprintf(out, size, "%.2f kHz", hz / 1000.0f);
        } else {
          snprintf(out, size, "%.1f kHz", hz / 1000.0f);
        }
        return;
      }
      case ToneParam::Level:
        snprintf(out, size, "%d dB", levelDb_);
        return;
      case ToneParam::Shape:
        if (waveform_ == Waveform::Square) {
          snprintf(out, size, "Duty %d%%", dutyPercent_);
        } else if (symmetryPercent_ == 0) {
          snprintf(out, size, "Rising");
        } else if (symmetryPercent_ == 50) {
          snprintf(out, size, "Triangle");
        } else if (symmetryPercent_ == 100) {
          snprintf(out, size, "Falling");
        } else {
          snprintf(out, size, "Shape %d%%", symmetryPercent_);
        }
        return;
    }
  }

  // Anything missing or out of range keeps its default rather than being
  // trusted.
  void load(playback::KeyValueStore &store) {
    uint8_t v = 0;
    if (store.getU8(kWaveKey, v) && v < kWaveformCount) {
      waveform_ = static_cast<Waveform>(v);
    }
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (store.getU8(kFreqHiKey, hi) && store.getU8(kFreqLoKey, lo)) {
      const int step = ((hi << 8) | lo) + kMinFrequencyStep;
      if (step >= kMinFrequencyStep && step <= kMaxFrequencyStep) frequencyStep_ = step;
    }
    if (store.getU8(kLevelKey, v) && v <= -kMinLevelDb) levelDb_ = -static_cast<int>(v);
    if (store.getU8(kDutyKey, v) && v >= kMinDutyPercent && v <= kMaxDutyPercent) {
      dutyPercent_ = v;
    }
    if (store.getU8(kSymmetryKey, v) && v <= 100) symmetryPercent_ = v;
  }

  void save(playback::KeyValueStore &store) const {
    const auto frequency = static_cast<uint16_t>(frequencyStep_ - kMinFrequencyStep);
    store.setU8(kWaveKey, static_cast<uint8_t>(waveform_));
    store.setU8(kFreqHiKey, static_cast<uint8_t>(frequency >> 8));
    store.setU8(kFreqLoKey, static_cast<uint8_t>(frequency & 0xFF));
    store.setU8(kLevelKey, static_cast<uint8_t>(-levelDb_));
    store.setU8(kDutyKey, static_cast<uint8_t>(dutyPercent_));
    store.setU8(kSymmetryKey, static_cast<uint8_t>(symmetryPercent_));
  }

 private:
  template <typename T>
  static bool set(T &field, T value) {
    if (field == value) return false;
    field = value;
    return true;
  }

  // 0 slow, 1 mid, 2 fast.
  int speedTier(int delta, uint32_t nowMs) {
    const uint32_t since = nowMs - lastTurnMs_;
    const bool first = !turnedBefore_;
    turnedBefore_ = true;
    lastTurnMs_ = nowMs;
    if (first || since >= kSpeedWindowMs) return 0;
    const uint32_t perSecond =
        static_cast<uint32_t>(std::abs(delta)) * 1000u / std::max<uint32_t>(since, 1);
    if (perSecond >= kFastDetentsPerSecond) return 2;
    if (perSecond >= kMidDetentsPerSecond) return 1;
    return 0;
  }

  // How far one detent goes at each speed. Frequency reaches 1/3 octave;
  // a waveform is never skipped.
  static int multiplier(ToneParam param, int tier) {
    static constexpr int kTable[kToneParamCount][3] = {
        {1, 1, 1},   // Waveform
        {1, 4, 16},  // Frequency
        {1, 1, 3},   // Level
        {1, 2, 5},   // Shape
    };
    return kTable[static_cast<int>(param)][tier];
  }

  Waveform waveform_ = Waveform::Sine;
  int frequencyStep_ = 0;
  int levelDb_ = kDefaultLevelDb;
  int dutyPercent_ = 50;
  int symmetryPercent_ = 0;
  ToneParam selected_ = ToneParam::Frequency;
  uint32_t lastTurnMs_ = 0;
  bool turnedBefore_ = false;
};

}  // namespace knobify::signal
```

- [ ] **Step 4: Run it to verify it passes**

Run: `pio test -e native -f test_tone_settings`
Expected: 9 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/signal/ToneSettings.h test/test_tone_settings
git commit -m "Add the tone generator's settings model" -m "Frequency on a 1/48-octave grid anchored at 1 kHz, level in whole dBFS, duty and saw symmetry, with the knob accelerating by detent speed. Which chips exist follows the waveform, so the screen only offers what does something." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Generator control and session

**Files:**
- Create: `lib/signal/GeneratorControl.h`, `lib/signal/ToneSession.h`
- Test: `test/test_tone_session/test_tone_session.cpp`

**Interfaces:**
- Consumes: `ToneSettings`, `ToneParam` (Task 3); `OscillatorParams` (Task 1); `playback::KeyValueStore`.
- Produces: `signal::kGeneratorSampleRate = 48000`; `class GeneratorOutput { virtual void apply(const OscillatorParams&)=0; virtual void start()=0; virtual void stop()=0; }`; `class GeneratorControl { void publish(const OscillatorParams&); void setRunning(bool); bool running() const; OscillatorParams snapshot() const; }`; `class ToneSession { ToneSession(GeneratorOutput&, playback::KeyValueStore&); void begin(); const ToneSettings &settings() const; void select(ToneParam); bool turn(int delta, uint32_t nowMs); void start(); void stop(); bool running() const; void tick(uint32_t nowMs); static constexpr uint32_t kSaveDebounceMs = 2000; }`

- [ ] **Step 1: Write the failing test** `test/test_tone_session/test_tone_session.cpp`

```cpp
#include <unity.h>

#include <map>
#include <string>

#include "GeneratorControl.h"
#include "ToneSession.h"

using knobify::playback::KeyValueStore;
using knobify::signal::GeneratorControl;
using knobify::signal::GeneratorOutput;
using knobify::signal::OscillatorParams;
using knobify::signal::ToneParam;
using knobify::signal::ToneSession;
using knobify::signal::ToneSettings;
using knobify::signal::Waveform;

void setUp() {}
void tearDown() {}

namespace {
class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override {
    values[key] = value;
    ++writes;
  }
  std::map<std::string, uint8_t> values;
  int writes = 0;
};

class FakeOutput : public GeneratorOutput {
 public:
  void apply(const OscillatorParams &p) override {
    last = p;
    ++applies;
  }
  void start() override { running = true; }
  void stop() override { running = false; }
  OscillatorParams last;
  int applies = 0;
  bool running = false;
};
}  // namespace

void test_begin_loads_and_applies_but_stays_silent() {
  FakeStore store;
  store.values[ToneSettings::kLevelKey] = 30;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  TEST_ASSERT_EQUAL_INT(-30, session.settings().levelDb());
  TEST_ASSERT_EQUAL_INT(1, out.applies);
  TEST_ASSERT_FALSE(out.running);
  TEST_ASSERT_FALSE(session.running());
}

void test_a_turn_reaches_the_output_at_once() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  TEST_ASSERT_TRUE(session.turn(48, 1000));  // One octave in one go.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 2000.0f, out.last.frequencyHz);
  TEST_ASSERT_FALSE(session.turn(0, 2000));
}

void test_start_and_stop() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  session.start();
  TEST_ASSERT_TRUE(out.running);
  TEST_ASSERT_TRUE(session.running());
  session.stop();
  TEST_ASSERT_FALSE(out.running);
  TEST_ASSERT_FALSE(session.running());
}

void test_saving_waits_until_the_knob_rests_or_the_tone_stops() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  session.select(ToneParam::Level);
  session.turn(1, 1000);
  session.turn(1, 1500);
  session.tick(3000);  // 1.5 s after the last turn.
  TEST_ASSERT_EQUAL_INT(0, store.writes);
  session.tick(3500);
  TEST_ASSERT_TRUE(store.writes > 0);
  TEST_ASSERT_EQUAL_UINT8(18, store.values[ToneSettings::kLevelKey]);

  const int before = store.writes;
  session.turn(1, 4000);
  session.stop();
  TEST_ASSERT_TRUE(store.writes > before);
  TEST_ASSERT_EQUAL_UINT8(17, store.values[ToneSettings::kLevelKey]);
}

void test_the_control_hands_parameters_across_intact() {
  GeneratorControl control;
  OscillatorParams p;
  p.waveform = Waveform::Saw;
  p.frequencyHz = 439.61f;
  p.amplitude = 0.001f;
  p.shape = 0.35f;
  control.publish(p);
  control.setRunning(true);
  const OscillatorParams q = control.snapshot();
  TEST_ASSERT_TRUE(control.running());
  TEST_ASSERT_EQUAL(Waveform::Saw, q.waveform);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 439.61f, q.frequencyHz);
  TEST_ASSERT_FLOAT_WITHIN(0.00005f, 0.001f, q.amplitude);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.35f, q.shape);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_loads_and_applies_but_stays_silent);
  RUN_TEST(test_a_turn_reaches_the_output_at_once);
  RUN_TEST(test_start_and_stop);
  RUN_TEST(test_saving_waits_until_the_knob_rests_or_the_tone_stops);
  RUN_TEST(test_the_control_hands_parameters_across_intact);
  return UNITY_END();
}
```

Note: `turn(48, 1000)` is the first turn, so it is slow tier (×1) and moves exactly 48 steps.

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f test_tone_session`
Expected: FAIL — `GeneratorControl.h: No such file or directory`.

- [ ] **Step 3: Write `lib/signal/GeneratorControl.h`**

```cpp
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "Waveform.h"

namespace knobify::signal {

// The rate the generator claims the DAC at (ADR 0024): high enough for a
// 20 kHz tone, and one the PCM5100A takes natively.
constexpr uint32_t kGeneratorSampleRate = 48000;

// Where a ToneSession sends its sound. The driver implements it; tests
// fake it. Callable from the main loop.
class GeneratorOutput {
 public:
  virtual ~GeneratorOutput() = default;
  virtual void apply(const OscillatorParams &params) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
};

// Hands the parameters from the main loop (core 1) to the audio task
// (core 0) without a lock: one atomic per field. A read that lands between
// two fields' stores uses one old and one new value for a single chunk
// (~2.7 ms), which nobody can hear -- the same trade AudioOutputStage's
// sample ring makes.
class GeneratorControl {
 public:
  void publish(const OscillatorParams &p) {
    waveform_.store(static_cast<uint8_t>(p.waveform), std::memory_order_relaxed);
    centiHz_.store(static_cast<uint32_t>(std::lround(p.frequencyHz * 100.0f)),
                   std::memory_order_relaxed);
    amplitude_.store(toUnit(p.amplitude), std::memory_order_relaxed);
    shape_.store(toUnit(p.shape), std::memory_order_relaxed);
  }

  void setRunning(bool running) { running_.store(running, std::memory_order_relaxed); }
  bool running() const { return running_.load(std::memory_order_relaxed); }

  OscillatorParams snapshot() const {
    OscillatorParams p;
    p.waveform = static_cast<Waveform>(waveform_.load(std::memory_order_relaxed));
    p.frequencyHz = centiHz_.load(std::memory_order_relaxed) / 100.0f;
    p.amplitude = fromUnit(amplitude_.load(std::memory_order_relaxed));
    p.shape = fromUnit(shape_.load(std::memory_order_relaxed));
    return p;
  }

 private:
  // 0..1 in 1/1,000,000ths: fine enough that -60 dB (0.001) is still
  // exact to a fraction of a dB, which Q15 is not.
  static constexpr float kUnit = 1000000.0f;
  static uint32_t toUnit(float v) {
    return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * kUnit));
  }
  static float fromUnit(uint32_t v) { return static_cast<float>(v) / kUnit; }

  std::atomic<uint8_t> waveform_{0};
  std::atomic<uint32_t> centiHz_{100000};
  std::atomic<uint32_t> amplitude_{0};
  std::atomic<uint32_t> shape_{500000};
  std::atomic<bool> running_{false};
};

}  // namespace knobify::signal
```

- [ ] **Step 4: Write `lib/signal/ToneSession.h`**

```cpp
#pragma once

#include <cstdint>

#include "GeneratorControl.h"
#include "KeyValueStore.h"
#include "ToneSettings.h"

namespace knobify::signal {

// One tone generator in use (ADR 0024): the settings, whether it sounds,
// and where the sound goes. Every change reaches the output at once;
// persisting waits until the knob has rested, so a sweep across the band
// is not a hundred flash writes. Pure logic -- pausing any music first is
// the screen's job, since only it holds the player.
class ToneSession {
 public:
  static constexpr uint32_t kSaveDebounceMs = 2000;

  ToneSession(GeneratorOutput &output, playback::KeyValueStore &store)
      : output_(output), store_(store) {}

  void begin() {
    settings_.load(store_);
    output_.apply(settings_.params());
  }

  const ToneSettings &settings() const { return settings_; }
  void select(ToneParam param) { settings_.select(param); }

  bool turn(int delta, uint32_t nowMs) {
    if (!settings_.turn(delta, nowMs)) return false;
    output_.apply(settings_.params());
    pendingSave_ = true;
    lastChangeMs_ = nowMs;
    return true;
  }

  void start() {
    if (running_) return;
    output_.apply(settings_.params());
    output_.start();
    running_ = true;
  }

  // Also saves anything still waiting: stopping is usually leaving.
  void stop() {
    if (running_) {
      output_.stop();
      running_ = false;
    }
    flush();
  }

  bool running() const { return running_; }

  void tick(uint32_t nowMs) {
    if (pendingSave_ && nowMs - lastChangeMs_ >= kSaveDebounceMs) flush();
  }

 private:
  void flush() {
    if (!pendingSave_) return;
    settings_.save(store_);
    pendingSave_ = false;
  }

  GeneratorOutput &output_;
  playback::KeyValueStore &store_;
  ToneSettings settings_;
  bool running_ = false;
  bool pendingSave_ = false;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace knobify::signal
```

- [ ] **Step 5: Run it to verify it passes**

Run: `pio test -e native -f test_tone_session`
Expected: 5 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/signal/GeneratorControl.h lib/signal/ToneSession.h test/test_tone_session
git commit -m "Add the tone session and its lock-free handover" -m "The session applies every change at once and saves only once the knob rests or the tone stops. GeneratorControl carries the parameters to the audio task in per-field atomics, the same no-lock trade as the sample ring." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Screen kind and knob routing

**Files:**
- Modify: `lib/navigation/ScreenId.h` (append after `Gravity,`)
- Modify: `lib/input/InputRouter.h`
- Test: `test/test_input/test_input.cpp`

**Interfaces:**
- Consumes: `signal::ToneSession` (Task 4).
- Produces: `navigation::ScreenKind::ToneGenerator`; `InputRouter::setToneSession(signal::ToneSession &)`.

- [ ] **Step 1: Write the failing test** — append to `test/test_input/test_input.cpp` (before `main`), add the includes/usings at the top, and register it in `main`:

```cpp
// Top of file, with the other includes:
#include "GeneratorControl.h"
#include "ToneSession.h"
// With the other usings:
using knobify::signal::GeneratorOutput;
using knobify::signal::OscillatorParams;
using knobify::signal::ToneSession;
```

```cpp
namespace {
class NullGeneratorOutput : public GeneratorOutput {
 public:
  void apply(const OscillatorParams &) override {}
  void start() override {}
  void stop() override {}
};
}  // namespace

void test_encoder_turns_the_tone_generators_selected_value() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::ToneGenerator, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);
  NullGeneratorOutput output;
  ToneSession session(output, store);
  session.begin();
  router.setToneSession(session);

  const uint8_t volumeBefore = playback.volume();
  router.onEncoderDelta(1, 1000);
  TEST_ASSERT_EQUAL_INT(1, session.settings().frequencyStep());
  TEST_ASSERT_EQUAL_UINT8(volumeBefore, playback.volume());
  TEST_ASSERT_EQUAL_INT(0, sink.calls);
}
```

```cpp
// In main():
  RUN_TEST(test_encoder_turns_the_tone_generators_selected_value);
```

- [ ] **Step 2: Run it to verify it fails**

Run: `pio test -e native -f test_input`
Expected: FAIL — `'ToneGenerator' is not a member of 'ScreenKind'`.

- [ ] **Step 3: Append the screen kind** in `lib/navigation/ScreenId.h`, directly after `Gravity,`:

```cpp
  // The tone generator (ADR 0024). Appended like everything above; past
  // NowPlaying, so a resume never lands on it -- a device that woke up
  // about to make a noise would be startling.
  ToneGenerator,
```

- [ ] **Step 4: Route the knob** in `lib/input/InputRouter.h`:

Add `#include "ToneSession.h"` to the includes. Add this case before `case navigation::ScreenKind::TouchCalibration:`:

```cpp
      case navigation::ScreenKind::ToneGenerator:
        // Whichever chip is selected (ADR 0024). Held here like brightness:
        // it is plain logic, and loop() only needs to redraw afterwards.
        if (toneSession_) toneSession_->turn(delta, nowMs);
        break;
```

Add a public setter after `onGesture()`:

```cpp
  // Optional, like a game's sounds: without one the knob does nothing on
  // the tone generator, which is all a host-side build needs.
  void setToneSession(signal::ToneSession &session) { toneSession_ = &session; }
```

And the member after `KnobSink &knobSink_;`:

```cpp
  signal::ToneSession *toneSession_ = nullptr;
```

- [ ] **Step 5: Run the input tests and the whole native suite**

Run: `pio test -e native -f test_input && pio test -e native`
Expected: all PASS (the navigation/resume tests must be unaffected by an appended kind).

- [ ] **Step 6: Commit**

```bash
git add lib/navigation/ScreenId.h lib/input/InputRouter.h test/test_input/test_input.cpp
git commit -m "Route the knob to the tone generator" -m "On the new ToneGenerator screen kind the knob adjusts whichever chip is selected. The kind is appended, past NowPlaying, so a resume never lands on it." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Audio path — generator output, unscaled writes, rate on resume

**Files:**
- Modify: `lib/drivers-audio/AudioOutputStage.h`, `lib/drivers-audio/AudioOutputStage.cpp`
- Modify: `lib/drivers-audio/ToneOutput.h`, `lib/drivers-audio/ToneOutput.cpp`
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.h` (`resume()`)

**Interfaces:**
- Consumes: `signal::Oscillator` (Task 1), `signal::GeneratorControl`, `signal::GeneratorOutput`, `signal::kGeneratorSampleRate` (Task 4).
- Produces: `drivers::ToneOutput` now also *is a* `signal::GeneratorOutput`; `AudioOutputStage::writeFramesUnscaled(const int16_t *interleaved, size_t frames)`; `AudioOutputStage::kSampleRingSize == 4096` (public constant).

These are hardware-bound; verification is the firmware build here and the device in Task 9.

- [ ] **Step 1: `AudioOutputStage.h`** — make the ring size public and 4096, and declare the unscaled write. Move `kSampleRingSize` out of `private:` into the public section, just above `readRecentSamples`:

```cpp
  // Samples kept for whoever reads them back: the Now Playing spectrum
  // takes its 1024, the tone generator's scope up to all of them -- two
  // periods of 20 Hz at 48 kHz need 4800, and 4096 shows most of that
  // (ADR 0024). Power of two.
  static constexpr size_t kSampleRingSize = 4096;
```

and below `writeFrames()`:

```cpp
  // The tone generator's path (ADR 0024): straight to the DAC with no
  // volume and no output gain, because its level is stated in dBFS and
  // must mean exactly that. Still recorded, so the scope sees it.
  // Blocks like writeFrames(); false on an I2S error.
  bool writeFramesUnscaled(const int16_t *interleaved, size_t frames);
```

Delete the old `static constexpr size_t kSampleRingSize = 1024;  // Power of two.` line from `private:`.

- [ ] **Step 2: `AudioOutputStage.cpp`** — add after `writeFrames()`:

```cpp
bool AudioOutputStage::writeFramesUnscaled(const int16_t *interleaved,
                                           size_t frames) {
  size_t done = 0;
  while (done < frames) {
    const size_t chunk = std::min(frames - done, kWriteChunkFrames);
    for (size_t i = 0; i < chunk; ++i) {
      noteMonoSample(static_cast<int16_t>(
          (static_cast<int32_t>(interleaved[(done + i) * 2]) +
           interleaved[(done + i) * 2 + 1]) /
          2));
    }
    size_t bytesWritten = 0;
    if (i2s_write(kI2sPort, interleaved + done * 2, chunk * 2 * sizeof(int16_t),
                  &bytesWritten, portMAX_DELAY) != ESP_OK) {
      return false;
    }
    done += chunk;
  }
  return true;
}
```

- [ ] **Step 3: `ToneOutput.h`** — make it a `GeneratorOutput` too.

Includes: add `#include "GeneratorControl.h"` and `#include "Oscillator.h"`.

Class line: `class ToneOutput : public games::BlipPlayer, public signal::GeneratorOutput {`

Update the class comment's first paragraph to add: `It also plays the tone generator (ADR 0024): while that runs, this task claims the port at 48 kHz and writes the oscillator instead of blips.`

Public, after `silence()`:

```cpp
  // signal::GeneratorOutput -- callable from the main loop. The audio
  // task picks each change up at its next chunk.
  void apply(const signal::OscillatorParams &params) override {
    control_.publish(params);
  }
  void start() override { control_.setRunning(true); }
  void stop() override { control_.setRunning(false); }
```

Private, next to `chunk_`:

```cpp
  // The generator's side of the handover and its voice. The oscillator is
  // touched only by this task.
  signal::GeneratorControl control_;
  signal::Oscillator oscillator_;
  int16_t mono_[kChunkFrames] = {};
  // The rate this task last clocked the port at, so switching between a
  // blip and the generator reprograms it but repeating either does not.
  uint32_t claimedRate_ = 0;
```

- [ ] **Step 4: `ToneOutput.cpp`** — replace the body of `taskLoop()` from `if (!stage.tone().active() || !streamIdle) {` through the end of the loop body with:

```cpp
    // The generator keeps writing through its fade-out after stop(), so
    // the last thing the DAC hears is silence rather than a cut.
    const bool generating = control_.running() || !oscillator_.idle();
    if ((!stage.tone().active() && !generating) || !streamIdle) {
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }

    const uint32_t rate = generating ? signal::kGeneratorSampleRate : kToneSampleRate;
    if (!rateIsOurs_ || claimedRate_ != rate) {
      // The port's rate is whatever the last track set. Claim it; a
      // decoder starting up, or a track resumed, sets its own again.
      i2s_set_sample_rates(kI2sPort, rate);
      stage.setSampleRate(rate);
      rateIsOurs_ = true;
      claimedRate_ = rate;
    }

    bool ok = true;
    if (generating) {
      oscillator_.setParams(control_.snapshot());
      if (control_.running()) {
        oscillator_.start();
      } else {
        oscillator_.stop();
      }
      oscillator_.render(mono_, kChunkFrames, rate);
      for (size_t i = 0; i < kChunkFrames; ++i) {
        chunk_[i * 2] = mono_[i];
        chunk_[i * 2 + 1] = mono_[i];
      }
#ifdef KNOBIFY_GENERATOR_DEBUG
      logGenerator(rate);
#endif
      ok = stage.writeFramesUnscaled(chunk_, kChunkFrames);
    } else {
      // writeFrames() mixes the blip in itself, so the music input is
      // silence and what reaches the DAC is the blip alone.
      for (size_t i = 0; i < kChunkFrames * 2; ++i) chunk_[i] = 0;
      ok = stage.writeFrames(chunk_, kChunkFrames);
    }
#if defined(KNOBIFY_TONE_DEBUG) || defined(KNOBIFY_GENERATOR_DEBUG)
    if (!ok) Serial.println("[tone] i2s write FAILED");
#else
    (void)ok;
#endif
    // Our own writes must not look like a decoder waking up.
    lastSeenSamples_ = stage.samplesWritten();
```

Keep the existing `KNOBIFY_TONE_DEBUG` measurement blocks that sat before the rate claim (the `measuring_` print and `rateClaimStart_` print) in place around the new rate claim, unchanged.

Add the debug helper (declared privately in `ToneOutput.h` under `#ifdef KNOBIFY_GENERATOR_DEBUG` as `void logGenerator(uint32_t rate);` plus members `uint32_t debugSamples_ = 0; uint32_t debugCrossings_ = 0; int16_t debugPeak_ = 0; int16_t debugLast_ = 0;`) at the end of `ToneOutput.cpp`, inside the namespace:

```cpp
#ifdef KNOBIFY_GENERATOR_DEBUG
// Once a second: the pitch and peak of what was actually written, measured
// from the samples rather than taken from the settings -- the check that
// the grid, the rate claim and the level all agree (ADR 0024).
void ToneOutput::logGenerator(uint32_t rate) {
  for (size_t i = 0; i < kChunkFrames; ++i) {
    const int16_t s = mono_[i];
    if (debugLast_ < 0 && s >= 0) ++debugCrossings_;
    debugLast_ = s;
    const int16_t magnitude = static_cast<int16_t>(s < 0 ? -s : s);
    if (magnitude > debugPeak_) debugPeak_ = magnitude;
  }
  debugSamples_ += kChunkFrames;
  if (debugSamples_ < rate) return;
  const float seconds = static_cast<float>(debugSamples_) / rate;
  const float dbfs = debugPeak_ > 0 ? 20.0f * log10f(debugPeak_ / 32767.0f) : -99.0f;
  Serial.printf("[generator] %.1f Hz, peak %d (%.1f dBFS) at %lu Hz\n",
                debugCrossings_ / seconds, debugPeak_, dbfs,
                static_cast<unsigned long>(rate));
  debugSamples_ = 0;
  debugCrossings_ = 0;
  debugPeak_ = 0;
}
#endif
```

(`<math.h>` is available through Arduino.h.)

- [ ] **Step 5: `Esp32AudioI2SDriver.h` `resume()`** — restore the track's rate before resuming, in both branches:

```cpp
  void resume() override {
    MutexGuard guard(mutex_);
    // Whatever used the port while this track was paused -- a game's
    // blips at 22.05 kHz, the tone generator at 48 kHz -- left its own
    // rate on it, and neither the library's pauseResume() nor the Vorbis
    // backend sets it again. Without this a paused track resumed after a
    // game played at half speed (ADR 0024).
    const uint32_t rate = vorbisActive_ ? vorbis_.sampleRate() : audio_.getSampleRate();
    if (rate != 0) {
      i2s_set_sample_rates(I2S_NUM_0, rate);
      audioOutputStage().setSampleRate(rate);
    }
    if (vorbisActive_) {
      vorbis_.setPaused(false);
      paused_ = false;
      return;
    }
    if (paused_) {
      audio_.pauseResume();
      paused_ = false;
    }
  }
```

(`driver/i2s.h` is already included by this header's users; if the compiler complains, add `#include <driver/i2s.h>` at the top.)

- [ ] **Step 6: Build the firmware and run the native suite**

Run: `pio run -e esp32-s3 && pio test -e native`
Expected: build SUCCESS, all tests PASS. Note the build's RAM line (`RAM: [...] used N bytes`) — the ring grew by 6 KB of internal RAM; record the number for Task 9.

- [ ] **Step 7: Commit**

```bash
git add lib/drivers-audio
git commit -m "Play the tone generator from the idle audio task" -m "ToneOutput now also renders the oscillator at 48 kHz while the generator runs, through a new unscaled write so the level means dBFS. The sample ring grows to 4096 so the scope sees low tones." -m "Resuming a paused track now restores its own I2S rate: after a game's blips it played at half speed, and after the generator it would play fast." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: The screen, the Home entry and the wiring

**Files:**
- Create: `lib/ui-widgets/ScopeTrace.h`, `lib/ui/ScreenManagerToneGenerator.cpp`
- Modify: `lib/ui/ScreenManager.h`, `lib/ui/ScreenManager.cpp`, `lib/ui/ScreenManagerMenu.cpp`
- Modify: `lib/ui-widgets/IconFont48.c`, `lib/ui-widgets/IconFont.h`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `ToneSession`, `ToneSettings`, `ToneParam`, `Waveform`, `TriggeredScope`, `kGeneratorSampleRate` (Tasks 1–4); `ScreenKind::ToneGenerator`, `InputRouter::setToneSession` (Task 5); `ToneOutput` as `GeneratorOutput`, `AudioOutputStage::kSampleRingSize` (Task 6).
- Produces: `ScreenManager::setToneSession`, `updateToneGeneratorDisplay()`, `tickToneGenerator(uint32_t nowMs, bool visible)`; `ui_widgets::ScopeTrace`; `KNOBIFY_ICON_TONES`.

- [ ] **Step 1: Write `lib/ui-widgets/ScopeTrace.h`**

```cpp
#pragma once

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace knobify::ui_widgets {

// An oscilloscope trace (ADR 0024): one lv_line over a zero line. A line
// rather than a canvas, for the reason ADR 0022 gives -- LVGL redraws only
// the line's area, where a canvas would re-blit its whole buffer.
// lv_line keeps a pointer to the points, so they live here.
class ScopeTrace {
 public:
  static constexpr size_t kPoints = 160;

  void create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
              lv_coord_t height, lv_color_t traceColor, lv_color_t zeroColor) {
    width_ = width;
    height_ = height;
    zeroPoints_ = {{{0, static_cast<lv_coord_t>(height / 2)},
                    {static_cast<lv_coord_t>(width - 1),
                     static_cast<lv_coord_t>(height / 2)}}};
    zero_ = makeLine(parent, x, y, 1, zeroColor);
    lv_line_set_points(zero_, zeroPoints_.data(), 2);
    line_ = makeLine(parent, x, y, 2, traceColor);
    clear();
  }

  // `values` holds kPoints samples; `fullScale` is the value drawn at the
  // band's top edge.
  void setSamples(const int16_t *values, size_t count, int32_t fullScale) {
    if (!line_ || count < 2 || fullScale <= 0) return;
    const size_t n = std::min(count, kPoints);
    const int32_t mid = height_ / 2;
    const int32_t half = height_ / 2 - 1;
    for (size_t i = 0; i < n; ++i) {
      const int32_t v = std::clamp<int32_t>(values[i] * half / fullScale, -half, half);
      points_[i].x = static_cast<lv_coord_t>(i * (width_ - 1) / (n - 1));
      points_[i].y = static_cast<lv_coord_t>(mid - v);
    }
    lv_line_set_points(line_, points_.data(), static_cast<uint16_t>(n));
  }

  // Flat on the zero line: nothing is sounding.
  void clear() {
    if (!line_) return;
    for (size_t i = 0; i < kPoints; ++i) {
      points_[i].x = static_cast<lv_coord_t>(i * (width_ - 1) / (kPoints - 1));
      points_[i].y = static_cast<lv_coord_t>(height_ / 2);
    }
    lv_line_set_points(line_, points_.data(), kPoints);
  }

  // The screen that owned the objects is being deleted.
  void detach() {
    line_ = nullptr;
    zero_ = nullptr;
  }

 private:
  static lv_obj_t *makeLine(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t lineWidth, lv_color_t color) {
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_width(line, lineWidth, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  }

  lv_obj_t *line_ = nullptr;
  lv_obj_t *zero_ = nullptr;
  lv_coord_t width_ = 0;
  lv_coord_t height_ = 0;
  std::array<lv_point_t, kPoints> points_{};
  std::array<lv_point_t, 2> zeroPoints_{};
};

}  // namespace knobify::ui_widgets
```

- [ ] **Step 2: `ScreenManager.h`** additions.

Includes: `#include "ScopeTrace.h"`, `#include "ToneSession.h"`.

Public, after `tickGravity()`:

```cpp
  // The tone generator (ADR 0024). Optional like the blips: without a
  // session the screen shows nothing to play.
  void setToneSession(signal::ToneSession &session) { toneSession_ = &session; }

  // Cheap redraw of the value and chips after a knob turn -- a no-op on
  // every other screen. Call after any encoder tick.
  void updateToneGeneratorDisplay();

  // Keeps the play button in step with the session (a lock can stop it)
  // and redraws the scope at ~30 fps while it is actually seen. Call
  // every loop().
  void tickToneGenerator(uint32_t nowMs, bool visible);
```

Private, after the Gravity helpers:

```cpp
  // ScreenManagerToneGenerator.cpp.
  void renderToneGenerator();
  void layoutToneChips();
  void applyTonePlayButton();
  static void onToneChipPressed(lv_event_t *e);
  static void onTonePlayClicked(lv_event_t *e);
```

Members, after the Gravity members:

```cpp
  // Tone generator (ADR 0024). Chips are created once per render in
  // kToneChipOrder and shown or hidden per waveform, never recreated on a
  // knob turn -- a chip being held while the knob turns must survive.
  struct ToneChipContext {
    ScreenManager *self = nullptr;
    signal::ToneParam param = signal::ToneParam::Frequency;
  };
  signal::ToneSession *toneSession_ = nullptr;
  std::array<lv_obj_t *, signal::kToneParamCount> toneChips_{};
  std::array<ToneChipContext, signal::kToneParamCount> toneChipContexts_{};
  lv_obj_t *toneValueLabel_ = nullptr;
  lv_obj_t *tonePlayButton_ = nullptr;
  ui_widgets::ScopeTrace toneScope_;
  // Over 4 KB, so it lands in PSRAM (see setup()'s allocation note);
  // allocated while the screen is shown, released when it is left.
  std::vector<int16_t> toneScopeSamples_;
  std::array<int16_t, ui_widgets::ScopeTrace::kPoints> toneScopeTrace_{};
  uint32_t lastToneScopeMs_ = 0;
  bool shownToneRunning_ = false;
  static constexpr uint32_t kToneScopeFrameMs = 33;
```

- [ ] **Step 3: `ScreenManager.cpp` `render()`** changes.

In the reset block, after `gravityThrustSounding_ = false;`:

```cpp
  toneChips_.fill(nullptr);
  toneValueLabel_ = nullptr;
  tonePlayButton_ = nullptr;
  toneScope_.detach();
```

After `Screen current = tabs_.activeStack().current();` (before the `renderedKind_` check):

```cpp
  // Leaving the tone generator silences it, whatever took you away.
  if (current.kind != ScreenKind::ToneGenerator) {
    if (toneSession_ && toneSession_->running()) toneSession_->stop();
    toneScopeSamples_.clear();
    toneScopeSamples_.shrink_to_fit();
  }
```

In the dispatch chain, after `} else if (current.kind == ScreenKind::SleepTimer) { renderSleepTimer();`:

```cpp
  } else if (current.kind == ScreenKind::ToneGenerator) {
    renderToneGenerator();
```

(Non-returning, like Brightness, so the back button and caption are drawn after it.)

In `captionTextFor()`, next to `case ScreenKind::Games: return "Games";`:

```cpp
    case ScreenKind::ToneGenerator:
      return "Tones";
```

- [ ] **Step 4: Write `lib/ui/ScreenManagerToneGenerator.cpp`**

```cpp
// ScreenManager's tone generator screen (ADR 0024), beside the other
// one-file-per-screen parts of ScreenManager.
//
// Top to bottom, all centred -- a round screen has no usable corners
// (ux-guidelines §7): back and caption, the scope, the selected value,
// the chips, and Play/Stop as the one primary button.

#include <lvgl.h>

#include <algorithm>

#include "AudioOutputStage.h"
#include "GeneratorControl.h"
#include "LvglButtonHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"
#include "ToneSettings.h"
#include "TriggeredScope.h"

namespace knobify::ui {

using signal::ToneParam;
using signal::ToneSettings;
using signal::Waveform;

namespace {

constexpr lv_coord_t kScopeWidth = 260;
constexpr lv_coord_t kScopeHeight = 100;
constexpr lv_coord_t kScopeY = 84;
constexpr lv_coord_t kValueY = 192;
constexpr lv_coord_t kChipY = 238;
constexpr lv_coord_t kChipWidth = 68;
constexpr lv_coord_t kChipHeight = 36;
constexpr lv_coord_t kChipGap = 6;
constexpr lv_coord_t kPlaySize = 56;
constexpr lv_coord_t kPlayY = 286;

// Noise has no pitch to lock on to; 20 ms of it reads as noise.
constexpr float kNoiseTimebaseHz = 100.0f;

constexpr ToneParam kToneChipOrder[signal::kToneParamCount] = {
    ToneParam::Waveform, ToneParam::Frequency, ToneParam::Level, ToneParam::Shape};

}  // namespace

void ScreenManager::renderToneGenerator() {
  if (!toneSession_) return;
  toneScopeSamples_.resize(drivers::AudioOutputStage::kSampleRingSize);

  toneScope_.create(screen_, (drivers::kLcdHorRes - kScopeWidth) / 2, kScopeY,
                    kScopeWidth, kScopeHeight, theme::ink(), theme::surfaceAlt());

  toneValueLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(toneValueLabel_, &knobify_text_font_28, 0);
  lv_obj_set_style_text_color(toneValueLabel_, theme::ink(), 0);
  lv_obj_align(toneValueLabel_, LV_ALIGN_TOP_MID, 0, kValueY);

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    toneChipContexts_[i] = ToneChipContext{this, kToneChipOrder[i]};
    lv_obj_t *chip = lv_btn_create(screen_);
    lv_obj_set_size(chip, kChipWidth, kChipHeight);
    lv_obj_set_ext_click_area(chip, kChipGap / 2);
    theme::styleSecondaryButton(chip);
    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, &knobify_text_font_16, 0);
    lv_obj_center(label);
    // On the press, not the click: holding a chip and turning the knob
    // with the other hand is this device's two-handed gesture (§7).
    lv_obj_add_event_cb(chip, onToneChipPressed, LV_EVENT_PRESSED,
                        &toneChipContexts_[i]);
    toneChips_[i] = chip;
  }

  tonePlayButton_ = makeIconButton(screen_, LV_SYMBOL_PLAY, kPlaySize, kPlaySize,
                                   LV_ALIGN_TOP_MID, 0, kPlayY, onTonePlayClicked,
                                   this, ButtonRole::Primary);

  lastToneScopeMs_ = lv_tick_get();
  applyTonePlayButton();
  updateToneGeneratorDisplay();
}

void ScreenManager::updateToneGeneratorDisplay() {
  if (!toneSession_ || !toneValueLabel_) return;
  const ToneSettings &settings = toneSession_->settings();
  char text[24];
  settings.valueText(settings.selected(), text, sizeof(text));
  lv_label_set_text(toneValueLabel_, text);
  layoutToneChips();
}

// Shows the chips this waveform has, centred as a row, the selected one
// filled in ink like a selected list row.
void ScreenManager::layoutToneChips() {
  const ToneSettings &settings = toneSession_->settings();
  const Waveform wave = settings.waveform();
  int shown = 0;
  for (ToneParam param : kToneChipOrder) shown += ToneSettings::visible(param, wave);
  const lv_coord_t rowWidth = shown * kChipWidth + (shown - 1) * kChipGap;
  lv_coord_t x = (drivers::kLcdHorRes - rowWidth) / 2;

  for (int i = 0; i < signal::kToneParamCount; ++i) {
    lv_obj_t *chip = toneChips_[i];
    if (!chip) continue;
    const ToneParam param = kToneChipOrder[i];
    if (!ToneSettings::visible(param, wave)) {
      lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(chip, x, kChipY);
    x += kChipWidth + kChipGap;
    const bool selected = settings.selected() == param;
    lv_obj_set_style_bg_color(chip, selected ? theme::ink() : theme::surfaceAlt(), 0);
    lv_obj_t *label = lv_obj_get_child(chip, 0);
    lv_obj_set_style_text_color(label, selected ? theme::surface() : theme::ink(), 0);
    lv_label_set_text(label, ToneSettings::chipLabel(param, wave));
  }
}

void ScreenManager::applyTonePlayButton() {
  if (!tonePlayButton_ || !toneSession_) return;
  shownToneRunning_ = toneSession_->running();
  lv_label_set_text(lv_obj_get_child(tonePlayButton_, 0),
                    shownToneRunning_ ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
}

void ScreenManager::onToneChipPressed(lv_event_t *e) {
  auto *context = static_cast<ToneChipContext *>(lv_event_get_user_data(e));
  if (!context || !context->self || !context->self->toneSession_) return;
  context->self->toneSession_->select(context->param);
  context->self->updateToneGeneratorDisplay();
}

void ScreenManager::onTonePlayClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  if (!self || !self->toneSession_) return;
  signal::ToneSession &session = *self->toneSession_;
  if (session.running()) {
    session.stop();
  } else {
    // A tone is for measuring, so it plays alone (ADR 0024). Paused, not
    // stopped: the track is still there to resume afterwards.
    if (self->playback_.state() == playback::PlaybackState::Playing) {
      self->playback_.togglePlayPause(lv_tick_get());
    }
    session.start();
  }
  self->applyTonePlayButton();
}

void ScreenManager::tickToneGenerator(uint32_t nowMs, bool visible) {
  if (!toneSession_ || !toneValueLabel_) return;
  if (shownToneRunning_ != toneSession_->running()) applyTonePlayButton();
  if (!visible) {
    lastToneScopeMs_ = nowMs;
    return;
  }
  if (nowMs - lastToneScopeMs_ < kToneScopeFrameMs) return;
  lastToneScopeMs_ = nowMs;

  const playback::SampleWindow window = playback_.readRecentSamples(
      toneScopeSamples_.data(), toneScopeSamples_.size());
  if (window.count == 0) {
    // Nothing new reached the DAC: stopped (or never started).
    if (!toneSession_->running()) toneScope_.clear();
    return;
  }
  const signal::OscillatorParams params = toneSession_->settings().params();
  const float hint =
      params.waveform == Waveform::Noise ? kNoiseTimebaseHz : params.frequencyHz;
  signal::TriggeredScope::trace(toneScopeSamples_.data(), window.count,
                                signal::kGeneratorSampleRate, hint,
                                toneScopeTrace_.data(), toneScopeTrace_.size());
  // Scaled to the set level, so the shape fills the band at any level --
  // the number below says how loud it is.
  const auto fullScale = std::max<int32_t>(
      1, static_cast<int32_t>(params.amplitude * 32767.0f));
  toneScope_.setSamples(toneScopeTrace_.data(), toneScopeTrace_.size(), fullScale);
}

}  // namespace knobify::ui
```

- [ ] **Step 5: The Home tile's glyph.** Pick a Material Symbols waveform glyph and add it to `IconFont48.c`:

```bash
cd "$(mktemp -d)"
curl -sLO 'https://raw.githubusercontent.com/google/material-design-icons/master/variablefont/MaterialSymbolsOutlined%5BFILL,GRAD,opsz,wght%5D.codepoints'
grep -E '^(airwave|waves|graphic_eq) ' *.codepoints
```

Use `airwave` if listed (a single wave line — reads as "a tone" where `graphic_eq` would read as the spectrum); otherwise `waves`. Then regenerate with the existing Opts plus the new codepoint (`0xNNNN` below), from the same font:

```bash
curl -sL -o MaterialSymbolsOutlined.ttf 'https://raw.githubusercontent.com/google/material-design-icons/master/variablefont/MaterialSymbolsOutlined%5BFILL,GRAD,opsz,wght%5D.ttf'
npx --yes lv_font_conv@latest --font MaterialSymbolsOutlined.ttf \
  --range 0xE405,0xE8B8,0xE518,0xEF44,0xEA19,0xEA66,0xEA28,0xNNNN \
  --size 48 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h \
  --lv-font-name knobify_icon_font_48 -o IconFont48.c
```

Replace everything in `lib/ui-widgets/IconFont48.c` **below** its header comment block with the new file's content below *its* header, keep the project header, update its `Opts:` line to the new range, and append to the header: `Extended 2026-09-18 (same source) with U+NNNN "<name>" for the Tones tile (ADR 0024).` Then check the old glyphs are byte-identical: `git diff --stat lib/ui-widgets/IconFont48.c` should show only additions around the new glyph plus the cmap/range tables.

Add to `lib/ui-widgets/IconFont.h` after `KNOBIFY_ICON_GAMES`:

```cpp
// The tone generator ("<name>"), ADR 0024: one wave line, which says
// "a tone" where the equalizer bars would say "spectrum".
#define KNOBIFY_ICON_TONES "<UTF-8 bytes of U+NNNN>"  // U+NNNN
```

(UTF-8 of a U+E000–U+FFFF codepoint is `\xEE..\x..\x..`; compute with `python3 -c "print(''.join('\\\\x%02X'%b for b in chr(0xNNNN).encode()))"`.)

- [ ] **Step 6: The Home entry** — append to `kMenuEntries` in `lib/ui/ScreenManagerMenu.cpp`, after the Games row (append-only: the row order is the `menuVis` bit order):

```cpp
    {KNOBIFY_ICON_TONES, "Tones",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::ToneGenerator, {}});
     }},
```

- [ ] **Step 7: Wire it up in `src/main.cpp`.**

Includes: `#include "ToneSession.h"`.

Globals, directly after `knobify::drivers::ToneOutput g_toneOutput;`:

```cpp
// The tone generator (ADR 0024): its settings and whether it sounds. The
// sound itself goes through g_toneOutput's task.
knobify::signal::ToneSession g_toneSession(g_toneOutput, g_nvsStore);
```

In `setup()`, directly after `g_brightness.begin();`:

```cpp
  g_toneSession.begin();
  g_inputRouter.setToneSession(g_toneSession);
```

and next to `g_screenManager.setBlipPlayer(g_toneOutput);`:

```cpp
    g_screenManager.setToneSession(g_toneSession);
```

In `loop()`, in the sleep-timer fade branch, inside `if (!g_sleepFading) {` after the message line:

```cpp
        // The generator bypasses the output gain the fade works through
        // (its level is dBFS), so it stops rather than fades.
        g_toneSession.stop();
```

After `g_screenManager.updateBrightnessDisplay();` in the encoder block:

```cpp
      g_screenManager.updateToneGeneratorDisplay();
```

After `g_brightness.tick(now);`:

```cpp
  g_toneSession.tick(now);
  // Locking silences a tone: the lock screen shows no Stop button, and a
  // pocket is no place for a 1 kHz sine.
  if (g_lockController.isLocked() && g_toneSession.running()) g_toneSession.stop();
  g_screenManager.tickToneGenerator(now, displayOn && !g_lockController.isLocked());
```

In `enterSleepTimerDeepSleep()`, before `g_resumeScheduler.saveNow(now);`:

```cpp
  g_toneSession.stop();
```

- [ ] **Step 8: Build and run the native suite**

Run: `scripts/check.sh`
Expected: `== check.sh OK ==`.

- [ ] **Step 9: Commit**

```bash
git add lib/ui-widgets/ScopeTrace.h lib/ui-widgets/IconFont48.c lib/ui-widgets/IconFont.h lib/ui src/main.cpp
git commit -m "Add Tones, the tone generator screen" -m "A seventh Home entry. Chips select what the knob adjusts, selected on the press so hold-and-turn works; only the chips a waveform has are shown. The scope draws what actually reached the DAC, triggered so it stands still. Starting a tone pauses music; leaving, locking or the sleep timer stop it." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Documentation

**Files:**
- Create: `docs/adr/0024-tone-generator.md`
- Modify: `docs/adr/README.md`, `docs/design/ux-guidelines.md`, `README.md`

- [ ] **Step 1: Write `docs/adr/0024-tone-generator.md`** in the house style (Status / Context / Decision with `###` sub-decisions / Consequences), covering, each with its reason as in the spec: the shared `lib/signal` layer and what the recorder and analyzer will reuse; chips selected on press; chips only where they act; the 1/48-octave grid anchored at 1 kHz and the acceleration tiers (marked "tuned on the device" with the final values); dBFS independent of volume and why the sleep fade stops rather than fades it; 48 kHz through the existing idle-writer task rather than a second task; PolyBLEP; the 5 ms ramps; the ring growing to 4096 (with the internal-RAM figure from Task 6); the scope scaled to the set level; music paused on start; the `resume()` rate fix and the game-blip bug it also fixes. Status: `Accepted — 2026-09-18`.

- [ ] **Step 2: Add the row** to `docs/adr/README.md`'s table:

```markdown
| [0024](0024-tone-generator.md) | Tone generator, and the shared signal layer | Accepted |
```

- [ ] **Step 3: `docs/design/ux-guidelines.md` §5** — in the main-menu bullet, extend the destination list with "Games, Tones"; in the "Context-sensitive encoder" bullet add: `on Tones it adjusts whichever chip is selected (ADR 0024);`.

- [ ] **Step 4: `README.md`** — add Tones to the feature list (one line: sine/square/saw/noise, 20 Hz–20 kHz, dBFS level, live scope), and to the backlog ("Explicitly out of scope for now") add: `Voice recorder and spectrum analyzer (microphone or own output), reusing lib/signal (ADR 0024).` and `Frequency sweep for the tone generator.`

- [ ] **Step 5: Commit**

```bash
git add docs README.md
git commit -m "Document the tone generator" -m "ADR 0024 records the decisions and the shared lib/signal layer the recorder and analyzer will build on." -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: On the device

Inline, with the user at the board (memory: UI iteration loop — flash, screenshot, serial capture; no subagents).

- [ ] **Step 1:** Flash with the `flash-device` skill using `PLATFORMIO_BUILD_FLAGS=-DKNOBIFY_GENERATOR_DEBUG`. Send `INFO` over serial; note `internal free` and compare with a pre-branch build. If internal RAM became tight, move the ring to PSRAM (`EXT_RAM_BSS_ATTR`) or drop it to 2048 and note it in ADR 0024.
- [ ] **Step 2:** Home → Tones. `SCREENSHOT` for Sine, Square, Saw, Noise; confirm nothing clips at the round edge on the physical screen (chips row and Play button are the tight ones).
- [ ] **Step 3:** ▶ at 1 kHz / −20 dB; serial must show `[generator] ~1000 Hz, peak ~3277 (-20.0 dBFS) at 48000 Hz`. Repeat at the grid ends (~20.3 Hz, ~19.9 kHz) and at 0 dB.
- [ ] **Step 4:** The user listens: no clicks on turning, starting, stopping or switching waveform; the acceleration feels right (tune `kMidDetentsPerSecond`, `kFastDetentsPerSecond` in `ToneSettings.h` by `KNOB n` injections and by hand; update the test in Task 3 if the tiers change).
- [ ] **Step 5:** Play a track, open Tones, ▶ → the music pauses. Back → silence. Resume the track from Now Playing → correct speed. Same after a Table Tennis game (the resume-rate fix).
- [ ] **Step 6:** Reboot → Tones shows the last waveform/frequency/level. Lock while sounding → silent; the play button shows ▶ after unlocking.
- [ ] **Step 7:** Commit any tuning, then use superpowers:finishing-a-development-branch.
