#pragma once

#include <cstdint>
#include <string>

namespace knobify::library {

// An embedded cover image found inside the audio file itself (currently
// only ID3v2 APIC frames in mp3s). `present` is only set for JPEG
// pictures -- other formats (e.g. PNG) are left unset since the cover-art
// pipeline only decodes JPEG. offset/length locate the raw image bytes
// within the source file so callers can read them out without the parser
// itself copying potentially large image data during a routine tag scan.
struct EmbeddedPicture {
  bool present = false;
  size_t offset = 0;
  size_t length = 0;
};

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
  uint16_t discNumber = 0;
  uint16_t year = 0;
  // Already resolved to a name: the numbered forms ID3 and MP4 still use
  // are looked up by the parsers (Id3Genres.h), so callers never see a
  // "(17)" here.
  std::string genre;
  EmbeddedPicture picture;
};

}  // namespace knobify::library
