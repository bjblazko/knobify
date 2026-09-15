#pragma once

#include <cstddef>
#include <cstdint>

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// What the player needs from an MP4/M4A file besides the audio itself.
struct Mp4Info {
  TagResult tags;
  // Exact, from the movie header; 0 when missing.
  uint32_t durationMs = 0;
  // Byte range of the top-level `mdat` atom's payload (the AAC frames);
  // both 0 when missing. The `moov` atom may sit before or after it.
  size_t mdatStart = 0;
  size_t mdatEnd = 0;
};

// Walks an MP4 file's atom tree by seeking, never loading more than one
// small value at a time: iTunes-style `ilst` tags (©nam, ©ART or aART,
// ©alb, trkn, disk, ©day), a JPEG `covr` picture's offset/length, the
// `mvhd` duration and the `mdat` range.
//
// ESP32-audioI2S only estimates an M4A's duration from its bitrate (256 s
// instead of 298 s for a real 256 kbps file, measured 2026-09-15), so the
// driver uses durationMs instead, like Mp3Duration for MP3s.
class Mp4Parser {
 public:
  static Mp4Info parse(RawFile &file);
};

}  // namespace knobify::library
