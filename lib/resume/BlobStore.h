#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace knobify::resume {

// Persistent storage for one small binary record per key. The concrete
// adapter (NvsKeyValueStore, lib/drivers-storage/) uses NVS, whose writes are
// atomic: after a power cut mid-write the key holds the old or the new value,
// never a mix (ADR 0012).
class BlobStore {
 public:
  virtual ~BlobStore() = default;
  // False (and `out` empty) if the key doesn't exist or can't be read.
  virtual bool getBlob(const std::string &key, std::vector<uint8_t> &out) = 0;
  virtual bool setBlob(const std::string &key, const std::vector<uint8_t> &data) = 0;
  virtual void removeBlob(const std::string &key) = 0;
};

}  // namespace knobify::resume
