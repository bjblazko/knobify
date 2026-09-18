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
