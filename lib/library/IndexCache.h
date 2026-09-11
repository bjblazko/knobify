#pragma once

#include <cstdint>
#include <vector>

#include "LibraryScanner.h"

namespace knobify::library {

// Cheap fingerprint of the SD card's current audio files, used to decide
// whether the cached index still matches reality without re-parsing any
// tags -- see docs/adr/0004-navigation-library-and-index-architecture.md.
struct LibrarySignature {
  uint32_t fileCount = 0;
  uint32_t sizeMtimeXor = 0;  // XOR of (size ^ mtime) across all files.

  bool operator==(const LibrarySignature &other) const {
    return fileCount == other.fileCount &&
           sizeMtimeXor == other.sizeMtimeXor;
  }
  bool operator!=(const LibrarySignature &other) const {
    return !(*this == other);
  }
};

LibrarySignature computeSignature(FileLister &lister);

// Encodes/decodes a LibraryIndex + its LibrarySignature to/from the
// binary /knobify/library.idx format. Operates on plain byte buffers
// (not a file interface) so the format itself is host-testable without
// mocking file I/O -- actual persistence to SD is a thin concern for the
// caller (main.cpp / a driver adapter).
class IndexCache {
 public:
  static std::vector<uint8_t> encode(const LibraryIndex &index,
                                      const LibrarySignature &signature);

  // Returns false if the buffer is missing, has a bad magic/version, or
  // is truncated/corrupt -- callers treat that identically to "no cache"
  // and fall back to a full rescan.
  static bool decode(const std::vector<uint8_t> &bytes, LibraryIndex &index,
                      LibrarySignature &signature);
};

}  // namespace knobify::library
