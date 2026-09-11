#pragma once

#include <string>

#include "RawFile.h"
#include "TagResult.h"

namespace knobify::library {

// Dispatches to the right format-specific parser by file extension, then
// applies the "Unknown Artist"/"Unknown Album"/filename-as-title
// fallback (decision 1, ADR 0004) so callers always get a usable result.
class TagReader {
 public:
  // filePath is used only for extension sniffing and the filename
  // fallback -- it does not need to be openable itself, `file` is what's
  // actually read.
  static TagResult read(RawFile &file, const std::string &filePath);
};

}  // namespace knobify::library
