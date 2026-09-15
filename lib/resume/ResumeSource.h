#pragma once

#include <cstdint>

#include "ResumeRecord.h"

namespace knobify::resume {

// One part of the device that can be resumed after a reboot: navigation,
// music, later a podcast/radio/video player (ADR 0012). Each owns its own
// section of the ResumeRecord.
class ResumeSource {
 public:
  virtual ~ResumeSource() = default;

  // Fills (or clears) this source's section with the current state.
  virtual void capture(ResumeRecord &record, uint32_t nowMs) = 0;

  // Applies this source's section, if present. The record passed its CRC
  // but may still name things that no longer exist (a rescanned library,
  // a removed SD card): restore what still resolves, skip the rest. Must
  // never start playback.
  virtual void restore(const ResumeRecord &record, uint32_t nowMs) = 0;
};

}  // namespace knobify::resume
