#pragma once

#include "LibraryScanner.h"  // for ScanProgressListener

namespace knobify::library {

// Triggers an on-demand library re-scan (signature check, full scan only
// if something actually changed) and updates whatever LibraryIndex the
// concrete implementation owns in place. Exists so ScreenManager can
// trigger a rescan from a UI button without depending on the concrete
// SD/file-opener types, which live in src/main.cpp.
class LibraryRescanner {
 public:
  virtual ~LibraryRescanner() = default;
  virtual void rescan(ScanProgressListener *progress) = 0;
};

}  // namespace knobify::library
