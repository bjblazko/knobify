#pragma once

#include <cstdint>

namespace knobify::games {

// How a game asks for a sound (ADR 0022). Deliberately this small: a game
// knows a pitch and a length, and nothing about I2S, tasks or whether
// music happens to be playing -- that is the driver's problem
// (docs/coding-guidelines.md's hardware/logic separation).
class BlipPlayer {
 public:
  virtual ~BlipPlayer() = default;
  virtual void blip(uint16_t frequencyHz, uint16_t durationMs) = 0;
  // Noise rather than a tone, for an engine rather than a blip (ADR
  // 0023). durationMs == 0 holds it until silence() -- thrust lasts as
  // long as the finger does, which no fixed duration can express.
  virtual void noise(uint16_t clockHz, uint16_t durationMs, int16_t level) = 0;
  // Stops anything sounding: leaving the game should not trail a beep,
  // and letting go of the throttle should not trail an engine.
  virtual void silence() = 0;
};

}  // namespace knobify::games
