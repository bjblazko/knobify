#pragma once

#include <cstdint>
#include <string>

namespace knobify::library {

// Output of any tag parser. `found` distinguishes "parsed successfully
// but the file simply has no tags" from "parsing failed" -- both leave
// artist/album/title empty, but TagReader's fallback logic only needs to
// know whether *any* usable metadata was found at all.
struct TagResult {
  bool found = false;
  std::string artist;
  std::string album;
  std::string title;
  uint16_t trackNumber = 0;
};

}  // namespace knobify::library
