#pragma once

#include <FS.h>

#include "RawFile.h"

namespace knobify::drivers {

// Thin RawFile adapter over an already-opened Arduino fs::File. The only
// place this project reads raw bytes from an SD-resident file for tag
// parsing -- see lib/library/RawFile.h and
// docs/adr/0004-navigation-library-and-index-architecture.md.
class SdRawFile : public library::RawFile {
 public:
  explicit SdRawFile(fs::File file) : file_(std::move(file)) {}

  size_t size() const override { return file_.size(); }
  bool seek(size_t position) override { return file_.seek(position); }
  size_t read(uint8_t *buf, size_t n) override { return file_.read(buf, n); }

 private:
  fs::File file_;
};

}  // namespace knobify::drivers
