#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace knobify::signal {

// A fine spectrum for measuring (ADR 0024): the level in dBFS in each of
// `columns` slices of 20 Hz .. 20 kHz on a log axis, so every octave is
// the same width and a doubled pitch is always the same step to the right.
//
// Not visualizer::SpectrumAnalyzer: that one lights twelve bands of a dot
// matrix to music and divides the volume out. This one reads absolute
// levels -- a -20 dB tone stands at -20 dB, and a square's odd harmonics
// stand where the maths says they should.
//
// Hann window over the newest kFftSize samples: at 48 kHz that is 23 Hz a
// bin, which is coarse in the bottom octaves (a column there falls between
// bins and is interpolated) and plenty everywhere else. A tone off a bin's
// centre reads up to ~1.4 dB low (Hann's scalloping), the price of a lobe
// narrow enough to tell 100 Hz from 200 Hz.
//
// Its ~32 KB of buffers are allocated on first use and given back by
// release(), so they come from PSRAM at run time and cost nothing while
// the screen is not shown.
class Spectrum {
 public:
  static constexpr size_t kFftSize = 2048;
  static constexpr float kLowHz = 20.0f;
  static constexpr float kHighHz = 20000.0f;
  static constexpr float kFloorDb = -100.0f;
  // A line that disappears falls rather than blinks out: 60 dB a second.
  static constexpr float kFallDbPerMs = 0.06f;

  explicit Spectrum(size_t columns) : levels_(columns, kFloorDb) {}

  size_t columns() const { return levels_.size(); }
  const std::vector<float> &levels() const { return levels_; }

  size_t columnFor(float hz) const {
    if (hz <= kLowHz) return 0;
    const float position = std::log(hz / kLowHz) / std::log(kHighHz / kLowHz);
    const auto column = static_cast<long>(position * static_cast<float>(columns()));
    return static_cast<size_t>(std::clamp<long>(column, 0, static_cast<long>(columns()) - 1));
  }

  // The newest `count` samples, oldest first. count == 0 (nothing new)
  // only lets the levels fall.
  void update(const int16_t *samples, size_t count, uint32_t sampleRate,
              uint32_t dtMs) {
    const float fall = kFallDbPerMs * static_cast<float>(dtMs);
    if (count == 0 || sampleRate == 0) {
      for (float &level : levels_) level = std::max(kFloorDb, level - fall);
      return;
    }
    allocate();
    if (count > kFftSize) {
      samples += count - kFftSize;
      count = kFftSize;
    }
    const size_t pad = kFftSize - count;
    for (size_t i = 0; i < kFftSize; ++i) {
      const float s = i < pad ? 0.0f : static_cast<float>(samples[i - pad]) / 32768.0f;
      re_[i] = s * window_[i];
      im_[i] = 0.0f;
    }
    fft();

    // Hann's coherent gain is 0.5, so a full-scale sine peaks at N/4.
    const float norm = 4.0f / static_cast<float>(kFftSize);
    for (size_t k = 0; k < kFftSize / 2; ++k) {
      const float mr = re_[k] * norm;
      const float mi = im_[k] * norm;
      re_[k] = mr * mr + mi * mi;  // Power, reusing the buffer.
    }
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(kFftSize);
    const float ratio = std::log(kHighHz / kLowHz) / static_cast<float>(columns());
    for (size_t c = 0; c < columns(); ++c) {
      const float loHz = kLowHz * std::exp(ratio * static_cast<float>(c));
      const float hiHz = kLowHz * std::exp(ratio * static_cast<float>(c + 1));
      const float power = columnPower(loHz / binHz, hiHz / binHz);
      const float db = power > 0.0f ? 10.0f * std::log10(power) : kFloorDb;
      levels_[c] = std::max({kFloorDb, db, levels_[c] - fall});
    }
  }

  // Gives the buffers back; the levels are kept.
  void release() {
    std::vector<float>().swap(re_);
    std::vector<float>().swap(im_);
    std::vector<float>().swap(window_);
    std::vector<float>().swap(cos_);
    std::vector<float>().swap(sin_);
  }

  void reset() { std::fill(levels_.begin(), levels_.end(), kFloorDb); }

 private:
  static constexpr double kTwoPi = 6.283185307179586;

  void allocate() {
    if (!re_.empty()) return;
    re_.assign(kFftSize, 0.0f);
    im_.assign(kFftSize, 0.0f);
    window_.resize(kFftSize);
    for (size_t i = 0; i < kFftSize; ++i) {
      window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(kTwoPi * i / (kFftSize - 1)));
    }
    cos_.resize(kFftSize / 2);
    sin_.resize(kFftSize / 2);
    for (size_t i = 0; i < kFftSize / 2; ++i) {
      cos_[i] = static_cast<float>(std::cos(kTwoPi * i / kFftSize));
      sin_[i] = static_cast<float>(-std::sin(kTwoPi * i / kFftSize));
    }
  }

  // The loudest bin in [loBin, hiBin), or -- for a column narrower than a
  // bin, low down -- the power interpolated at its centre.
  float columnPower(float loBin, float hiBin) const {
    const size_t last = kFftSize / 2 - 1;
    auto first = static_cast<size_t>(std::ceil(loBin));
    auto end = static_cast<size_t>(std::ceil(hiBin));
    end = std::min(end, last + 1);
    if (first < end) {
      float power = 0.0f;
      for (size_t k = first; k < end; ++k) power = std::max(power, re_[k]);
      return power;
    }
    const float centre = std::min((loBin + hiBin) * 0.5f, static_cast<float>(last));
    const auto k = static_cast<size_t>(centre);
    const float frac = centre - static_cast<float>(k);
    const size_t next = std::min(k + 1, last);
    return re_[k] + (re_[next] - re_[k]) * frac;
  }

  // In-place iterative radix-2 FFT over re_/im_.
  void fft() {
    for (size_t i = 1, j = 0; i < kFftSize; ++i) {
      size_t bit = kFftSize >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) {
        std::swap(re_[i], re_[j]);
        std::swap(im_[i], im_[j]);
      }
    }
    for (size_t len = 2; len <= kFftSize; len <<= 1) {
      const size_t half = len / 2;
      const size_t step = kFftSize / len;
      for (size_t i = 0; i < kFftSize; i += len) {
        for (size_t k = 0; k < half; ++k) {
          const float wr = cos_[k * step];
          const float wi = sin_[k * step];
          const size_t a = i + k;
          const size_t b = a + half;
          const float tr = re_[b] * wr - im_[b] * wi;
          const float ti = re_[b] * wi + im_[b] * wr;
          re_[b] = re_[a] - tr;
          im_[b] = im_[a] - ti;
          re_[a] += tr;
          im_[a] += ti;
        }
      }
    }
  }

  std::vector<float> levels_;
  std::vector<float> re_;
  std::vector<float> im_;
  std::vector<float> window_;
  std::vector<float> cos_;
  std::vector<float> sin_;
};

}  // namespace knobify::signal
