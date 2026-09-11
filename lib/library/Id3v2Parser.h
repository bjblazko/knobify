#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Parses ID3v2.3/2.4 frames (TIT2/TPE1/TALB/TRCK) from the start of an
// MP3 file, falling back to a trailing 128-byte ID3v1 tag if no ID3v2
// header is present. Text encodings ISO-8859-1 and UTF-8 are passed
// through as-is; UTF-16 (with or without BOM) is naively downconverted
// by dropping null bytes -- good enough for the common ASCII-range case,
// not a full Unicode implementation.
class Id3v2Parser {
 public:
  static TagResult parse(RawFile &file);
};

}  // namespace knobify::library
