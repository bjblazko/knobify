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
    noise_.store(static_cast<uint8_t>(p.noise), std::memory_order_relaxed);
  }

  void setRunning(bool running) { running_.store(running, std::memory_order_relaxed); }
  bool running() const { return running_.load(std::memory_order_relaxed); }

  OscillatorParams snapshot() const {
    OscillatorParams p;
    p.waveform = static_cast<Waveform>(waveform_.load(std::memory_order_relaxed));
    p.frequencyHz = centiHz_.load(std::memory_order_relaxed) / 100.0f;
    p.amplitude = fromUnit(amplitude_.load(std::memory_order_relaxed));
    p.shape = fromUnit(shape_.load(std::memory_order_relaxed));
    p.noise = static_cast<NoiseColor>(noise_.load(std::memory_order_relaxed));
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
  std::atomic<uint8_t> noise_{0};
  std::atomic<bool> running_{false};
};

}  // namespace knobify::signal
