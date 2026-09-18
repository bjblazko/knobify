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
  // Stops anything sounding: leaving the game should not trail a beep.
  virtual void silence() = 0;
};

}  // namespace knobify::games
