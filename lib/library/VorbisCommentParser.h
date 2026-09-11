#pragma once

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Extracts ARTIST/ALBUM/TITLE/TRACKNUMBER from an OGG Vorbis comment
// header. Rather than fully parsing Ogg page framing, this scans the
// first 64KB of the file for the "\x03vorbis" comment-packet magic and
// reads the comment list that follows it directly -- correct for the
// overwhelming majority of real files, where the comment header (unlike
// large embedded cover art, which this project doesn't need to read)
// fits in a single Ogg page. A comment header split across page
// boundaries is a documented limitation, not silently mishandled: it
// simply won't be found, and TagReader's normal fallback applies.
class VorbisCommentParser {
 public:
  static TagResult parse(RawFile &file);
};

}  // namespace knobify::library
