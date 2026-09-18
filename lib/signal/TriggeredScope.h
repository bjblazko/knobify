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
