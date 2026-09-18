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
//
// Between samples the trace is *reconstructed*, not joined up: a windowed
// sinc, which is what the DAC's own reconstruction filter does, so the
// trace is what comes out of the jack. Straight lines between samples drew
// a 15 kHz sine -- 3.2 samples a period -- as a zigzag. The same goes for
// where the trigger lands: it is refined on the reconstructed signal,
// since a crossing placed by a straight line between two far-apart
// samples made high tones shimmer.
//
// Pure logic. It knows nothing about where the samples came from -- the
// DAC's ring now, the microphone for the recorder and analyzer later.
class TriggeredScope {
 public:
  static constexpr float kPeriods = 2.0f;
  // Sinc taps either side of a point. Enough for a clean sine up to ~0.9
  // of Nyquist; a trace is 160 points, so 32 multiplies each is nothing.
  static constexpr int kTaps = 16;

  // Two periods of the hinted frequency; frequencyHintHz <= 0 estimates
  // the period from the zero crossings.
  static void trace(const int16_t *samples, size_t count, uint32_t sampleRate,
                    float frequencyHintHz, int16_t *out, size_t width) {
    if (width == 0) return;
    const int peak = count >= 2 && sampleRate != 0 ? peakOf(samples, count) : 0;
    if (peak == 0) {
      std::fill(out, out + width, 0);
      return;
    }
    float hz = frequencyHintHz;
    if (hz <= 0.0f) hz = estimateHz(samples, count, sampleRate, hysteresisFor(peak));
    const float span = hz > 0.0f ? kPeriods * static_cast<float>(sampleRate) / hz
                                 : static_cast<float>(count);
    traceWindow(samples, count, span, out, width);
  }

  // `spanSamples` wide -- a timebase the caller chose (ScopeScale).
  static void traceWindow(const int16_t *samples, size_t count, float spanSamples,
                          int16_t *out, size_t width) {
    if (width == 0) return;
    const int peak = count >= 2 ? peakOf(samples, count) : 0;
    if (peak == 0) {
      std::fill(out, out + width, 0);
      return;
    }
    // The kernel needs kTaps samples either side of every point it draws.
    const float first = static_cast<float>(kTaps);
    const float last = static_cast<float>(count) - 1.0f - kTaps;
    if (last - first < 2.0f) {
      std::fill(out, out + width, 0);
      return;
    }
    const float span = std::clamp(spanSamples, 2.0f, last - first);

    float start = latestTrigger(samples, count, first, last - span,
                                hysteresisFor(peak));
    if (start < 0.0f) {
      start = last - span;
    } else {
      start = refineCrossing(samples, count, start);
    }

    for (size_t i = 0; i < width; ++i) {
      const float pos =
          width == 1 ? start
                     : start + span * static_cast<float>(i) /
                                   static_cast<float>(width - 1);
      const long v = std::lround(reconstruct(samples, count, pos));
      out[i] = static_cast<int16_t>(std::clamp(v, -32768L, 32767L));
    }
  }

 private:
  static constexpr float kPi = 3.14159265358979f;

  static int peakOf(const int16_t *s, size_t count) {
    int peak = 0;
    for (size_t i = 0; i < count; ++i) {
      peak = std::max(peak, std::abs(static_cast<int>(s[i])));
    }
    return peak;
  }

  static int hysteresisFor(int peak) { return std::max(peak / 8, 16); }

  // The band-limited signal at a fractional position: a sinc under a
  // squared Welch window, which reaches zero at +-kTaps.
  static float reconstruct(const int16_t *s, size_t count, float pos) {
    const auto base = static_cast<long>(std::floor(pos));
    const float frac = pos - static_cast<float>(base);
    if (frac < 1e-4f) return s[std::clamp<long>(base, 0, static_cast<long>(count) - 1)];
    // sin(pi * (frac - j)) is +-sin(pi * frac): one sine per point.
    const float sinPiFrac = std::sin(kPi * frac);
    float sum = 0.0f;
    for (int j = -kTaps + 1; j <= kTaps; ++j) {
      const long k = base + j;
      if (k < 0 || k >= static_cast<long>(count)) continue;
      const float d = frac - static_cast<float>(j);
      const float sinc = ((j & 1) ? -sinPiFrac : sinPiFrac) / (kPi * d);
      const float x = d / kTaps;
      const float window = (1.0f - x * x) * (1.0f - x * x);
      sum += static_cast<float>(s[k]) * sinc * window;
    }
    return sum;
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

  // The newest crossing in [from, to].
  static float latestTrigger(const int16_t *s, size_t count, float from, float to,
                             int hysteresis) {
    float best = -1.0f;
    forEachCrossing(s, count, hysteresis, [&](float at) {
      if (at >= from && at <= to) best = at;
    });
    return best;
  }

  // Moves a straight-line crossing estimate onto the reconstructed
  // signal's own zero, by a few secant steps kept within the sample gap.
  static float refineCrossing(const int16_t *s, size_t count, float at) {
    const float lo = std::floor(at);
    const float hi = lo + 1.0f;
    float x0 = lo;
    float x1 = hi;
    float f0 = reconstruct(s, count, x0);
    float f1 = reconstruct(s, count, x1);
    if (!(f0 < 0.0f && f1 >= 0.0f)) return at;
    for (int i = 0; i < 6; ++i) {
      const float x = x0 - f0 * (x1 - x0) / (f1 - f0);
      const float f = reconstruct(s, count, x);
      if (f < 0.0f) {
        x0 = x;
        f0 = f;
      } else {
        x1 = x;
        f1 = f;
      }
    }
    return x0 - f0 * (x1 - x0) / (f1 - f0);
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
