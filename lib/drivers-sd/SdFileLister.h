#pragma once

#include <Arduino.h>
#include <SD_MMC.h>

#include <string>
#include <vector>

#include "AudioFileTypes.h"
#include "FileLister.h"

namespace knobify::drivers {

// Recursively lists every audio file (see AudioFileTypes.h) under a root path on
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
    dirsVisited_ = 0;
    filesVisited_ = 0;
    appleDoubleSkipped_ = 0;

    fs::File dir = SD_MMC.open(root_.c_str());
    if (!dir) {
      // TEMPORARY DIAGNOSTIC (2026-09-12): investigating "only a
      // handful of tracks found" reports on real hardware -- see
      // AGENTS.md. Remove once the SD reliability issue is resolved.
      Serial.printf("SdFileLister: could not open root '%s' at all\n",
                    root_.c_str());
    } else if (!dir.isDirectory()) {
      Serial.printf("SdFileLister: root '%s' exists but is not a directory\n",
                    root_.c_str());
    } else {
      walk(dir);
      // Found on real hardware 2026-09-13: unlike every child entry
      // (closed in walk()), this top-level handle was never closed --
      // each reset() call (i.e. every tap of the rescan button) leaked
      // one of SD_MMC's small fixed pool of open-file slots. After
      // enough taps in a session, the pool is exhausted and every
      // subsequent SD_MMC.open() anywhere in the app fails outright
      // (`sdmmc_read_blocks failed`), not just here -- see AGENTS.md.
      dir.close();
    }
    Serial.printf(
        "SdFileLister: root='%s' dirsVisited=%u filesVisited=%u "
        "appleDoubleSkipped=%u audioCandidatesFound=%u\n",
        root_.c_str(), static_cast<unsigned>(dirsVisited_),
        static_cast<unsigned>(filesVisited_),
        static_cast<unsigned>(appleDoubleSkipped_),
        static_cast<unsigned>(entries_.size()));
  }

  // No re-walk -- just replays the entries reset() already collected.
  void rewind() override { index_ = 0; }

  bool next(library::FileEntry &out) override {
    if (index_ >= entries_.size()) return false;
    out = entries_[index_++];
    return true;
  }

 private:
  // macOS creates a "._<name>" AppleDouble sidecar file next to any file
  // or folder it copies (e.g. a real "06 Merge.mp3" gets a
  // "._06 Merge.mp3" alongside it) -- these are metadata blobs, not real
  // audio, but they otherwise pass isAudioFileName() since they keep the
  // real file's extension. Without this check they get scanned as
  // "tracks" that fail to read correctly. Found on real hardware
  // 2026-09-12 (a library copied via a Mac).
  //
  // Takes the same string entry.name() returns for isAudioFileName() --
  // this project's SD_MMC/FS stack returns the FULL PATH from name()
  // (not just the filename, despite the Arduino File API convention
  // elsewhere), so the sidecar prefix must be checked against the
  // substring after the last '/', not the start of the whole string.
  // The first version of this check compared against the full path's
  // start and so never matched anything -- found by seeing the exact
  // fopen() failures this was supposed to prevent still appearing in
  // the log after deploying it, real hardware 2026-09-12.
  static bool isAppleDoubleSidecar(const std::string &nameOrPath) {
    auto slash = nameOrPath.find_last_of('/');
    const std::string &base = nameOrPath;
    size_t start = (slash == std::string::npos) ? 0 : slash + 1;
    return nameOrPath.size() >= start + 2 && base[start] == '.' &&
           base[start + 1] == '_';
  }

  void walk(fs::File &dir) {
    for (fs::File entry = dir.openNextFile(); entry;
         entry = dir.openNextFile()) {
      std::string name = entry.name();
      if (isAppleDoubleSidecar(name)) {
        ++appleDoubleSkipped_;
        // Skip entirely -- neither recurse into it (for a directory's
        // sidecar) nor consider it a track candidate.
      } else if (entry.isDirectory()) {
        ++dirsVisited_;
        walk(entry);
      } else {
        ++filesVisited_;
        if (library::isAudioFileName(name)) {
          entries_.push_back(library::FileEntry{
              entry.path(), static_cast<uint32_t>(entry.size()),
              static_cast<uint32_t>(entry.getLastWrite())});
        }
      }
      entry.close();
    }
  }

  std::string root_;
  std::vector<library::FileEntry> entries_;
  size_t index_ = 0;
  // TEMPORARY DIAGNOSTIC (2026-09-12): see reset(). Remove alongside it.
  size_t dirsVisited_ = 0;
  size_t filesVisited_ = 0;
  size_t appleDoubleSkipped_ = 0;
};

}  // namespace knobify::drivers
