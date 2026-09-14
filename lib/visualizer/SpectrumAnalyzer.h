#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace knobify::visualizer {

// Turns a window of recent mono PCM samples into dot-matrix column heights
// for the Now Playing spectrum (ADR 0009). Pure logic, host-tested: Hann
// window, 1024-point FFT, 12 log-spaced bands from 60 Hz to 16 kHz, dB
// scaling, instant attack and a linear fall-off.
//
// `gain` is the linear volume gain the samples were already multiplied by
// on their way to the DAC; it's divided out so turning the volume knob
// doesn't shrink the bars.
class SpectrumAnalyzer {
 public:
  static constexpr size_t kFftSize = 1024;
  static constexpr size_t kBands = 12;
  static constexpr uint8_t kMaxLevel = 12;
  using Levels = std::array<uint8_t, kBands>;

  static constexpr float kLowHz = 60.0f;
  static constexpr float kHighHz = 16000.0f;
  // Band power (0 dB = a full-scale sine) mapped linearly onto 0..kMaxLevel.
  static constexpr float kFloorDb = -60.0f;
  static constexpr float kCeilDb = -6.0f;
  // Full height to empty in 300 ms.
  static constexpr float kFallLevelsPerMs = kMaxLevel / 300.0f;

  SpectrumAnalyzer() {
    for (size_t i = 0; i < kFftSize; ++i) {
      window_[i] = 0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) /
                                          static_cast<float>(kFftSize - 1));
    }
  }

  // Band index a frequency falls into (clamped to the first/last band).
  static size_t bandForFrequency(float hz) {
    if (hz <= kLowHz) return 0;
    float position = std::log(hz / kLowHz) / std::log(kHighHz / kLowHz);
    long band = static_cast<long>(position * kBands);
    if (band < 0) return 0;
    if (band >= static_cast<long>(kBands)) return kBands - 1;
    return static_cast<size_t>(band);
  }

  // `samples` holds the most recent `count` samples, oldest first; fewer
  // than kFftSize are zero-padded in front, more keep only the newest.
  // Anything unusable (no samples, no sample rate, muted) just decays.
  void update(const int16_t *samples, size_t count, uint32_t sampleRate,
              float gain, uint32_t dtMs) {
    decay(dtMs);
    if (count == 0 || sampleRate == 0 || gain <= 0.0f) return;
    if (count > kFftSize) {
      samples += count - kFftSize;
      count = kFftSize;
    }
    size_t pad = kFftSize - count;
    float scale = 1.0f / (32768.0f * gain);
    for (size_t i = 0; i < kFftSize; ++i) {
      float s = i < pad ? 0.0f : static_cast<float>(samples[i - pad]) * scale;
      re_[i] = s * window_[i];
      im_[i] = 0.0f;
    }
    fft();
    if (sampleRate != binsForRate_) computeBandBins(sampleRate);

    // Hann coherent gain is 0.5, so a full-scale sine peaks at N/4.
    float norm = 4.0f / static_cast<float>(kFftSize);
    for (size_t band = 0; band < kBands; ++band) {
      float power = 0.0f;
      for (size_t k = bandLo_[band]; k < bandHi_[band]; ++k) {
        float mr = re_[k] * norm;
        float mi = im_[k] * norm;
        power += mr * mr + mi * mi;
      }
      float db = 10.0f * std::log10(power + 1e-12f);
      float target = (db - kFloorDb) / (kCeilDb - kFloorDb) * kMaxLevel;
      if (target > levels_[band]) levels_[band] = target;
      if (levels_[band] > kMaxLevel) levels_[band] = kMaxLevel;
    }
  }

  void decay(uint32_t dtMs) {
    float fall = kFallLevelsPerMs * static_cast<float>(dtMs);
    for (float &level : levels_) {
      level = level > fall ? level - fall : 0.0f;
    }
  }

  Levels levels() const {
    Levels out{};
    for (size_t i = 0; i < kBands; ++i) {
      out[i] = static_cast<uint8_t>(std::lround(levels_[i]));
    }
    return out;
  }

 private:
  static constexpr float kTwoPi = 6.2831853f;

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
      float angle = -kTwoPi / static_cast<float>(len);
      float wr = std::cos(angle);
      float wi = std::sin(angle);
      for (size_t start = 0; start < kFftSize; start += len) {
        float cr = 1.0f;
        float ci = 0.0f;
        for (size_t k = 0; k < len / 2; ++k) {
          size_t a = start + k;
          size_t b = a + len / 2;
          float tr = re_[b] * cr - im_[b] * ci;
          float ti = re_[b] * ci + im_[b] * cr;
          re_[b] = re_[a] - tr;
          im_[b] = im_[a] - ti;
          re_[a] += tr;
          im_[a] += ti;
          float nr = cr * wr - ci * wi;
          ci = cr * wi + ci * wr;
          cr = nr;
        }
      }
    }
  }

  // Each band covers FFT bins [lo, hi); every band gets at least one bin,
  // so the narrow bass bands still show something at 43 Hz resolution.
  // Bands above Nyquist (low-sample-rate files) get none and stay dark.
  void computeBandBins(uint32_t sampleRate) {
    binsForRate_ = sampleRate;
    float binHz = static_cast<float>(sampleRate) / kFftSize;
    size_t nyquistBin = kFftSize / 2;
    float ratio = std::pow(kHighHz / kLowHz, 1.0f / kBands);
    float edge = kLowHz;
    for (size_t band = 0; band < kBands; ++band) {
      size_t lo = static_cast<size_t>(std::lround(edge / binHz));
      edge *= ratio;
      size_t hi = static_cast<size_t>(std::lround(edge / binHz));
      if (lo < 1) lo = 1;
      if (hi <= lo) hi = lo + 1;
      if (lo > nyquistBin) lo = nyquistBin;
      if (hi > nyquistBin) hi = nyquistBin;
      bandLo_[band] = lo;
      bandHi_[band] = hi;
    }
  }

  std::array<float, kFftSize> window_{};
  std::array<float, kFftSize> re_{};
  std::array<float, kFftSize> im_{};
  std::array<float, kBands> levels_{};
  std::array<size_t, kBands> bandLo_{};
  std::array<size_t, kBands> bandHi_{};
  uint32_t binsForRate_ = 0;
};

}  // namespace knobify::visualizer
