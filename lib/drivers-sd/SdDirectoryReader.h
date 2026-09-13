#pragma once

#include <SD_MMC.h>

#include <string>
#include <vector>

#include "FolderBrowser.h"

namespace knobify::drivers {

// Lists one directory's immediate children for Files-mode browsing
// (lib/library/FolderBrowser) -- always live, never cached, per decision
// 10 in docs/adr/0004-navigation-library-and-index-architecture.md.
class SdDirectoryReader : public library::DirectoryReader {
 public:
  std::vector<library::FolderEntry> listChildren(
      const std::string &path) override {
    std::vector<library::FolderEntry> result;
    fs::File dir = SD_MMC.open(path.c_str());
    if (!dir || !dir.isDirectory()) {
      return result;
    }
    for (fs::File entry = dir.openNextFile(); entry;
         entry = dir.openNextFile()) {
      // This project's SD_MMC/FS stack returns the full path from
      // entry.name() (not just the filename, despite the usual Arduino
      // File API convention) -- found alongside the same issue in
      // SdFileLister's AppleDouble-sidecar check, real hardware
      // 2026-09-12. Without stripping it here, FolderEntry.name would
      // hold a full path, breaking both display and the child-path
      // building in ScreenManager.
      result.push_back(
          library::FolderEntry{basename(entry.name()), entry.isDirectory()});
      entry.close();
    }
    return result;
  }

 private:
  static std::string basename(const std::string &nameOrPath) {
    auto slash = nameOrPath.find_last_of('/');
    return slash == std::string::npos ? nameOrPath
                                       : nameOrPath.substr(slash + 1);
  }
};

}  // namespace knobify::drivers
