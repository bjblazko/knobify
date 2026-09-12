#pragma once

#include <SD_MMC.h>

#include <string>
#include <vector>

#include "FileLister.h"

namespace knobify::drivers {

// Recursively lists every audio file (mp3/ogg/wav) under a root path on
// the SD card, for the tag-based library scan (lib/library/LibraryScanner).
// Not a hot path (runs once at boot, or when the cache is stale), so it
// eagerly walks the whole tree into memory on reset() rather than
// maintaining open directory handles across next() calls -- simpler and
// good enough for realistic library sizes on an 8MB-PSRAM board.
class SdFileLister : public library::FileLister {
 public:
  explicit SdFileLister(std::string root) : root_(std::move(root)) {}

  void reset() override {
    entries_.clear();
    index_ = 0;
    fs::File dir = SD_MMC.open(root_.c_str());
    if (dir && dir.isDirectory()) {
      walk(dir);
    }
  }

  bool next(library::FileEntry &out) override {
    if (index_ >= entries_.size()) return false;
    out = entries_[index_++];
    return true;
  }

 private:
  static bool isAudioFile(const std::string &name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    for (auto &c : ext) c = static_cast<char>(tolower(c));
    return ext == "mp3" || ext == "ogg" || ext == "wav";
  }

  // macOS creates a "._<name>" AppleDouble sidecar file next to any file
  // or folder it copies (e.g. a real "06 Merge.mp3" gets a
  // "._06 Merge.mp3" alongside it) -- these are metadata blobs, not real
  // audio, but they otherwise pass isAudioFile() since they keep the
  // real file's extension. Without this check they get scanned as
  // "tracks" that fail to read correctly. Found on real hardware
  // 2026-09-12 (a library copied via a Mac).
  static bool isAppleDoubleSidecar(const std::string &name) {
    return name.size() >= 2 && name[0] == '.' && name[1] == '_';
  }

  void walk(fs::File &dir) {
    for (fs::File entry = dir.openNextFile(); entry;
         entry = dir.openNextFile()) {
      std::string name = entry.name();
      if (isAppleDoubleSidecar(name)) {
        // Skip entirely -- neither recurse into it (for a directory's
        // sidecar) nor consider it a track candidate.
      } else if (entry.isDirectory()) {
        walk(entry);
      } else if (isAudioFile(name)) {
        entries_.push_back(library::FileEntry{
            entry.path(), static_cast<uint32_t>(entry.size()),
            static_cast<uint32_t>(entry.getLastWrite())});
      }
      entry.close();
    }
  }

  std::string root_;
  std::vector<library::FileEntry> entries_;
  size_t index_ = 0;
};

}  // namespace knobify::drivers
