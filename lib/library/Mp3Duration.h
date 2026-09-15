#pragma once

#include <cstdint>

#include "RawFile.h"

namespace knobify::library {

// Exact duration of an MP3 from the frame count in its Xing/Info or VBRI
// header, the header encoders write into the first audio frame.
//
// ESP32-audioI2S ignores that header and estimates a duration from the
// average bitrate of the first ~200 frames. The whole library is VBR
// (checked 2026-09-15), and quiet intros encode at a low bitrate, so the
// estimate started minutes too long and shrank over the first seconds of
// every track. Pure logic over RawFile, host-tested like the tag parsers.
class Mp3Duration {
 public:
  // Whole seconds (rounded); 0 when the file has no usable VBR header --
  // callers then fall back to the decoder's own estimate.
  static uint32_t readSeconds(RawFile &file);
};

}  // namespace knobify::library
