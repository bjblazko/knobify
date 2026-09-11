#pragma once

#include <string>
#include <vector>

namespace knobify::library {

struct FolderEntry {
  std::string name;  // Just the entry's own name, not a full path.
  bool isDirectory;
};

// Lists a single directory's immediate children. Separate from
// FileLister (which recursively walks the whole card for the tag-based
// scan) because Files-mode browsing is on-demand, uncached, and only
// ever needs one level at a time -- see decision 10 in
// docs/adr/0004-navigation-library-and-index-architecture.md. Concrete
// adapter (SdDirectoryReader) lives in lib/drivers-sd/.
class DirectoryReader {
 public:
  virtual ~DirectoryReader() = default;
  virtual std::vector<FolderEntry> listChildren(const std::string &path) = 0;
};

// Thin pass-through that also filters to audio files (by extension) plus
// subfolders, so FolderScreen only ever sees things worth showing.
class FolderBrowser {
 public:
  static std::vector<FolderEntry> list(DirectoryReader &reader,
                                        const std::string &path);
};

}  // namespace knobify::library
