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
      result.push_back(
          library::FolderEntry{entry.name(), entry.isDirectory()});
      entry.close();
    }
    return result;
  }
};

}  // namespace knobify::drivers
