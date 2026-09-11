#pragma once

#include <cstdint>

#include "KeyValueStore.h"

namespace knobify::playback {

// Loads/saves a single persisted volume level so a physical volume knob
// doesn't reset to a default every boot (decision 9, ADR 0004). Writes
// are the caller's responsibility to debounce (PlaybackStateMachine only
// calls save() after volume changes settle) to avoid wearing the flash
// with every knob tick.
class VolumePersistence {
 public:
  static constexpr char kKey[] = "volume";
  static constexpr uint8_t kDefaultVolume = 10;  // Mid-range of 0-21.

  explicit VolumePersistence(KeyValueStore &store) : store_(store) {}

  uint8_t load() const {
    uint8_t value = kDefaultVolume;
    store_.getU8(kKey, value);
    return value;
  }

  void save(uint8_t volume) { store_.setU8(kKey, volume); }

 private:
  KeyValueStore &store_;
};

}  // namespace knobify::playback
