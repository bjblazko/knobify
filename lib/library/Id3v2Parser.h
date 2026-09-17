#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Parses ID3v2.3/2.4 frames (TIT2/TPE1/TALB/TRCK) from the start of an
// MP3 file, falling back to a trailing 128-byte ID3v1 tag if no ID3v2
// header is present. All four ID3v2 text encodings are decoded to full
// UTF-8 (see Utf8.h): ISO-8859-1 is transcoded byte-for-byte, UTF-8 is
// repaired if it turns out to actually be Latin-1 in disguise, and
// UTF-16 (with BOM, or big-endian without one) is decoded in full,
// surrogate pairs included.
class Id3v2Parser {
 public:
  static TagResult parse(RawFile &file);
};

}  // namespace knobify::library
