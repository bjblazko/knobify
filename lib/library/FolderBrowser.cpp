#include "FolderBrowser.h"

#include <algorithm>
#include <cctype>

namespace knobify::library {

namespace {

bool isAudioFile(const std::string &name) {
  auto dot = name.find_last_of('.');
  if (dot == std::string::npos) return false;
  std::string ext = name.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                  [](unsigned char c) { return std::tolower(c); });
  return ext == "mp3" || ext == "ogg" || ext == "wav";
}

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
    if (entry.isDirectory || isAudioFile(entry.name)) {
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
