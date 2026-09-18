#pragma once

#include <cstdint>

#include "PongGame.h"

namespace knobify::games {

// Pong's three blips (ADR 0022).
//
// The original has no sound chip: its tones are taps off the same counter
// chain that divides the 7.159 MHz master clock down to video sync, so
// every pitch is that chain's ~15.7 kHz horizontal line rate divided by a
// power of two. The three constants below are those divisions, which is
// the part of the original's sound that is a matter of arithmetic.
//
// Which division drives which event is the part taken by ear rather than
// from a schematic, and published accounts disagree -- so treat the
// assignment here as tunable, and the divisions themselves as fixed.
// Longest and lowest for the point conceded, shortest and highest for the
// paddle, is how the machine is remembered.
struct Blip {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

constexpr uint16_t kLineRateHz = 15720;

constexpr Blip kPaddleBlip{kLineRateHz / 16, 24};  // ~982 Hz
constexpr Blip kWallBlip{kLineRateHz / 32, 24};    // ~491 Hz
constexpr Blip kScoreBlip{kLineRateHz / 64, 240};  // ~246 Hz

constexpr Blip blipFor(PongGame::Sound sound) {
  switch (sound) {
    case PongGame::Sound::Paddle:
      return kPaddleBlip;
    case PongGame::Sound::Wall:
      return kWallBlip;
    case PongGame::Sound::Score:
      return kScoreBlip;
    default:
      return Blip{0, 0};
  }
}

}  // namespace knobify::games
