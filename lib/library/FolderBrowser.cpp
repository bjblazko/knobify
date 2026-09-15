#include "FolderBrowser.h"

#include <algorithm>
#include <cctype>

#include "AudioFileTypes.h"

namespace knobify::library {

namespace {

// macOS AppleDouble sidecar (e.g. "._06 Merge.mp3") -- see
// lib/drivers-sd/SdFileLister.h's isAppleDoubleSidecar() for why this
// needs to be filtered separately from the extension check.
bool isAppleDoubleSidecar(const std::string &name) {
  return name.size() >= 2 && name[0] == '.' && name[1] == '_';
}

}  // namespace

std::vector<FolderEntry> FolderBrowser::list(DirectoryReader &reader,
                                              const std::string &path) {
  std::vector<FolderEntry> all = reader.listChildren(path);
  std::vector<FolderEntry> filtered;
  for (auto &entry : all) {
    if (isAppleDoubleSidecar(entry.name)) continue;
    if (entry.isDirectory || isAudioFileName(entry.name)) {
      filtered.push_back(entry);
    }
  }

  // Folders first, then files, alphabetically within each group -- the
  // conventional file-browser ordering.
  std::sort(filtered.begin(), filtered.end(),
            [](const FolderEntry &a, const FolderEntry &b) {
              if (a.isDirectory != b.isDirectory) return a.isDirectory;
              return a.name < b.name;
            });
  return filtered;
}

}  // namespace knobify::library
