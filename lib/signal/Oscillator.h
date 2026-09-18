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
        return noise();
    }
    return 0.0f;
  }

  // One sample of the chosen colour, unit RMS, scaled so its RMS sits
  // kNoiseCrestDb under full scale, then limited to full scale. Every
  // colour is built from the same Gaussian white, so they are equally
  // loud, and the rare peak past 4 sigma is clipped rather than letting
  // the level mean something else for noise than for a tone.
  float noise() {
    const float white = gaussian();
    float coloured = white;
    switch (params_.noise) {
      case NoiseColor::White:
        break;
      case NoiseColor::Pink:
        coloured = pink(white) * kPinkGain;
        break;
      case NoiseColor::Brown:
        // A leaky integrator: -6 dB an octave down to ~8 Hz, and no
        // wandering off to one rail.
        brown_ = kBrownLeak * brown_ + white;
        coloured = brown_ * kBrownGain;
        break;
      case NoiseColor::Blue: {
        // Differentiating adds +6 dB an octave: pink's -3 becomes +3.
        const float p = pink(white);
        coloured = (p - lastPink_) * kBlueGain;
        lastPink_ = p;
        break;
      }
      case NoiseColor::Violet:
        coloured = (white - lastWhite_) * kVioletGain;
        lastWhite_ = white;
        break;
    }
    return std::clamp(coloured * kNoiseRms, -1.0f, 1.0f);
  }

  // xorshift32, as a uniform sample in [-1, 1).
  float uniform() {
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return static_cast<float>(static_cast<int32_t>(noise_)) * (1.0f / 2147483648.0f);
  }

  // Four uniforms summed: close enough to Gaussian for noise that is
  // listened to and looked at, and unit variance after scaling.
  float gaussian() {
    return (uniform() + uniform() + uniform() + uniform()) * kGaussianScale;
  }

  // Paul Kellet's refined pink filter: -3 dB an octave to within 0.05 dB
  // above 9 Hz.
  float pink(float white) {
    pink_[0] = 0.99886f * pink_[0] + white * 0.0555179f;
    pink_[1] = 0.99332f * pink_[1] + white * 0.0750759f;
    pink_[2] = 0.96900f * pink_[2] + white * 0.1538520f;
    pink_[3] = 0.86650f * pink_[3] + white * 0.3104856f;
    pink_[4] = 0.55000f * pink_[4] + white * 0.5329522f;
    pink_[5] = -0.7616f * pink_[5] - white * 0.0168980f;
    const float out = pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] +
                      pink_[5] + pink_[6] + white * 0.5362f;
    pink_[6] = white * 0.115926f;
    return out;
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

  // RMS 12 dB under the level: Gaussian noise then passes 4 sigma -- and
  // is clipped -- about once in 16,000 samples.
  static constexpr float kNoiseRms = 0.25f;
  static constexpr float kGaussianScale = 0.8660254f;  // 1 / sqrt(4/3)
  static constexpr float kBrownLeak = 0.998f;
  // Each colour's filter output back to unit RMS: 1/sigma of the filter
  // fed unit Gaussian white, measured over 100 s (brown's is also
  // sqrt(1 - leak^2)). test_oscillator holds every colour to the same RMS.
  static constexpr float kPinkGain = 0.32764f;
  static constexpr float kBrownGain = 0.06317f;
  static constexpr float kBlueGain = 0.55137f;
  static constexpr float kVioletGain = 0.70741f;

  std::array<float, kSineTableSize + 1> sine_{};
  std::array<float, 7> pink_{};
  float brown_ = 0.0f;
  float lastPink_ = 0.0f;
  float lastWhite_ = 0.0f;
  OscillatorParams params_;
  uint32_t phase_ = 0;
  uint32_t noise_ = 0x9E3779B9u;
  float gain_ = 0.0f;
  bool running_ = false;
};

}  // namespace knobify::signal
