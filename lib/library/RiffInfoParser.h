#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Extracts title/artist/album from a WAV file's RIFF "LIST"/"INFO" chunk
// (INAM/IART/IPRD sub-chunks). WAV has no standard track-number tag, so
// trackNumber is always left at 0 here.
class RiffInfoParser {
 public:
  static TagResult parse(RawFile &file);
};

}  // namespace knobify::library
