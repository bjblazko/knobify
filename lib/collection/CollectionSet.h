#pragma once

#include <string>

#include "CollectionId.h"
#include "CollectionProfile.h"
#include "LibraryScanner.h"  // LibraryIndex, ScanProgressListener

namespace knobify::collection {

// The device's collections, as the UI sees them: each one's index, its
// profile, and a way to rebuild it on demand.
//
// Replaces library::LibraryRescanner, which existed for the same reason
// -- to let ScreenManager trigger a rescan without depending on the
// concrete SD/file-opener types, which live in src/main.cpp. Now that the
// UI also needs to reach one of several indexes, both concerns are the
// same interface: "the collections this device has".
class CollectionSet {
 public:
  virtual ~CollectionSet() = default;

  // The (possibly empty) index for a collection. Never null: a collection
  // whose folder or cache is missing is simply empty, exactly as a card
  // with no /Music was before.
  virtual library::LibraryIndex &index(CollectionId id) = 0;

  const library::LibraryIndex &index(CollectionId id) const {
    return const_cast<CollectionSet *>(this)->index(id);
  }

  // Signature check, then a full scan only if something actually changed.
  // Blocking and multi-second -- see ScreenManager::runRescan()'s comment
  // about what a UI handler may do while it runs.
  virtual void rescan(CollectionId id,
                      library::ScanProgressListener *progress) = 0;

  const CollectionProfile &profile(CollectionId id) const {
    return profileOf(id);
  }

  // Which collection a file belongs to, by path. Roots never nest (the
  // collection tests enforce it), so this is unambiguous -- which is why
  // nothing has to remember "the collection playback was started from"
  // and keep it in sync.
  bool findByPath(const std::string &path, CollectionId &out) const {
    for (const auto &candidate : kCollections) {
      const std::string root = candidate.rootPath;
      if (path.size() > root.size() && path.compare(0, root.size(), root) == 0 &&
          path[root.size()] == '/') {
        out = candidate.id;
        return true;
      }
    }
    return false;
  }
};

}  // namespace knobify::collection
