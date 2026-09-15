#pragma once

#include <Preferences.h>

#include <vector>

#include "BlobStore.h"
#include "KeyValueStore.h"

namespace knobify::drivers {

// Wraps ESP32 Preferences (NVS) for the handful of small persisted
// values this project needs (volume, settings, the resume record) -- see
// lib/playback/KeyValueStore.h and lib/resume/BlobStore.h.
class NvsKeyValueStore : public playback::KeyValueStore,
                         public resume::BlobStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    prefs_.begin(kNamespace, /*readOnly=*/true);
    bool found = prefs_.isKey(key.c_str());
    if (found) {
      out = prefs_.getUChar(key.c_str());
    }
    prefs_.end();
    return found;
  }

  void setU8(const std::string &key, uint8_t value) override {
    prefs_.begin(kNamespace, /*readOnly=*/false);
    prefs_.putUChar(key.c_str(), value);
    prefs_.end();
  }

  // NVS writes a changed blob to fresh entries and only then invalidates
  // the old ones, so a power cut leaves one or the other (ADR 0012).
  bool getBlob(const std::string &key, std::vector<uint8_t> &out) override {
    out.clear();
    if (!prefs_.begin(kNamespace, /*readOnly=*/true)) return false;
    size_t length = prefs_.isKey(key.c_str()) ? prefs_.getBytesLength(key.c_str()) : 0;
    if (length > 0) {
      out.resize(length);
      if (prefs_.getBytes(key.c_str(), out.data(), length) != length) out.clear();
    }
    prefs_.end();
    return !out.empty();
  }

  bool setBlob(const std::string &key, const std::vector<uint8_t> &data) override {
    if (!prefs_.begin(kNamespace, /*readOnly=*/false)) return false;
    bool ok = prefs_.putBytes(key.c_str(), data.data(), data.size()) == data.size();
    prefs_.end();
    return ok;
  }

  void removeBlob(const std::string &key) override {
    if (!prefs_.begin(kNamespace, /*readOnly=*/false)) return;
    prefs_.remove(key.c_str());
    prefs_.end();
  }

 private:
  static constexpr const char *kNamespace = "knobify";
  Preferences prefs_;
};

}  // namespace knobify::drivers
