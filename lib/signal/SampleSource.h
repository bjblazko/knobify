#pragma once

#include <cstddef>
#include <cstdint>

namespace knobify::signal {

// Where a scope gets its samples (ADR 0024): the DAC's ring for the tone
// generator now, the microphone for the recorder and analyzer later.
//
// Deliberately not PlaybackStateMachine::readRecentSamples(): that one
// answers only while music plays, so a paused track's last buffer never
// looks live on the spectrum -- and the tone generator pauses the music
// before it sounds (found on the device 2026-09-18: a flat scope under a
// measured 1 kHz tone).
class SampleSource {
 public:
  virtual ~SampleSource() = default;
  // Copies up to maxSamples of the newest mono samples, oldest first, and
  // returns how many; 0 when nothing new arrived since the last read.
  virtual size_t readRecent(int16_t *dst, size_t maxSamples) = 0;
};

}  // namespace knobify::signal
