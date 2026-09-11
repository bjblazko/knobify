#pragma once

#include <Preferences.h>

#include "KeyValueStore.h"

namespace knobify::drivers {

// Wraps ESP32 Preferences (NVS) for the handful of small persisted
// values this project needs (volume, later gesture-hint "seen" flags) --
// see lib/playback/KeyValueStore.h.
class NvsKeyValueStore : public playback::KeyValueStore {
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

 private:
  static constexpr const char *kNamespace = "knobify";
  Preferences prefs_;
};

}  // namespace knobify::drivers
