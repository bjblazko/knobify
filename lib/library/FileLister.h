#pragma once

#include <cstdint>
#include <string>

namespace knobify::library {

struct FileEntry {
  std::string path;  // Full path, e.g. "/Music/Artist/Album/01 Song.mp3".
  uint32_t size = 0;
  uint32_t mtime = 0;  // Unix-ish timestamp; only used for change detection.
};

// Recursively yields every audio file (by extension: mp3/ogg/wav) under
// the SD card's music root. Concrete adapter (SdFileLister) lives in
// lib/drivers-sd/ and wraps Arduino's SD API; this interface exists so
// LibraryScanner/FolderBrowser are host-testable against a fake.
class FileLister {
 public:
  virtual ~FileLister() = default;

  // Restarts iteration from the beginning.
  virtual void reset() = 0;

  // Advances to the next entry; returns false when exhausted.
  virtual bool next(FileEntry &out) = 0;
};

}  // namespace knobify::library
