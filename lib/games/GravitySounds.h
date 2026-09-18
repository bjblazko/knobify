#pragma once

#include <cstdint>

#include "GravityGame.h"
#include "ToneGenerator.h"

namespace knobify::games {

// Gravity's sounds (ADR 0023).
//
// The engine is noise, not a tone: a rocket is a rumble, and a square
// wave is a beep however low it is pitched. It is also held rather than
// fired -- thrust lasts as long as the finger does -- which is why
// ToneGenerator grew a sustained mode for this game.
//
// The two events are one-shots. Touchdown is a plain tone, because
// arriving is a clean sound; the crash is the engine's noise again, lower
// and louder, which is what a wreck sounds like when all you have is a
// shift register.
struct GravityBlip {
  uint16_t frequencyHz;  // Square-wave pitch, or the noise clock.
  uint16_t durationMs;   // 0 means held until silenced.
  int16_t level;
  bool noise;
};

// Held, and quieter than a blip: a blip's level is chosen to cut through
// music for 24ms, and the same level running continuously under a flight
// would be exhausting.
constexpr GravityBlip kThrustNoise{1400, 0, playback::ToneGenerator::kAmplitude / 3,
                                   true};
constexpr GravityBlip kTouchdownTone{880, 180,
                                     playback::ToneGenerator::kAmplitude, false};
constexpr GravityBlip kCrashNoise{320, 600, playback::ToneGenerator::kAmplitude,
                                  true};

constexpr GravityBlip blipFor(GravityGame::Sound sound) {
  switch (sound) {
    case GravityGame::Sound::Touchdown:
      return kTouchdownTone;
    case GravityGame::Sound::Crash:
      return kCrashNoise;
    default:
      return GravityBlip{0, 0, 0, false};
  }
}

}  // namespace knobify::games
