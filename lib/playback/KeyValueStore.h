#pragma once

#include <cstdint>
#include <string>

namespace knobify::playback {

// Minimal persistent key-value surface for small bits of state that
// should survive a reboot (volume, and later the gesture-hint "seen"
// flags in lib/ui/). Concrete adapter (NvsKeyValueStore,
// lib/drivers-storage/) wraps ESP32 Preferences/NVS.
class KeyValueStore {
 public:
  virtual ~KeyValueStore() = default;
  virtual bool getU8(const std::string &key, uint8_t &out) = 0;
  virtual void setU8(const std::string &key, uint8_t value) = 0;
};

}  // namespace knobify::playback
