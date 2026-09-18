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
