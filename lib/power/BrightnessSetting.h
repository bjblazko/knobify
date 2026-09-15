#pragma once

#include <array>
#include <cstdint>

#include "KeyValueStore.h"

namespace knobify::power {

// The display-brightness setting (ADR 0010): a level from kMinLevel to
// kMaxLevel (shown as 10%..100%), adjusted by the encoder on the
// Brightness screen and persisted like volume -- only once it has settled,
// to avoid a flash write per detent. Pure logic over an explicit clock;
// main.cpp drives the backlight PWM from duty().
class BrightnessSetting {
 public:
  static constexpr char kKey[] = "brightness";
  static constexpr uint8_t kMinLevel = 1;
  static constexpr uint8_t kMaxLevel = 10;
  static constexpr uint8_t kDefaultLevel = kMaxLevel;  // Full, as before.
  static constexpr uint32_t kSaveDebounceMs = 1000;

  explicit BrightnessSetting(playback::KeyValueStore &store) : store_(store) {}

  // Loads the persisted level (default: full brightness). Out-of-range
  // stored values are clamped rather than trusted.
  void begin() {
    uint8_t stored = kDefaultLevel;
    level_ = store_.getU8(kKey, stored) ? clamp(stored) : kDefaultLevel;
  }

  void adjust(int delta, uint32_t nowMs) {
    uint8_t next = clamp(static_cast<int>(level_) + delta);
    if (next == level_) return;
    level_ = next;
    pendingSave_ = true;
    lastChangeMs_ = nowMs;
  }

  // Call every loop(); persists the level once unchanged for
  // kSaveDebounceMs.
  void tick(uint32_t nowMs) {
    if (pendingSave_ && nowMs - lastChangeMs_ >= kSaveDebounceMs) {
      store_.setU8(kKey, level_);
      pendingSave_ = false;
    }
  }

  uint8_t level() const { return level_; }
  uint8_t percent() const { return level_ * 10; }

  // 8-bit backlight PWM duty for the current level. Spaced on a gamma-2.2
  // curve from a readable floor to full, so each step looks equally large
  // (linear duty steps all bunch up at the bright end), and never 0: the
  // lowest setting must not be mistaken for the display being off.
  // Values: round(kFloor + (255 - kFloor) * ((level - 1) / 9) ^ 2.2).
  uint8_t duty() const { return kDuty[level_ - kMinLevel]; }

 private:
  static constexpr std::array<uint8_t, kMaxLevel - kMinLevel + 1> kDuty = {
      10, 12, 19, 32, 51, 77, 110, 151, 199, 255};

  static uint8_t clamp(int level) {
    if (level < kMinLevel) return kMinLevel;
    if (level > kMaxLevel) return kMaxLevel;
    return static_cast<uint8_t>(level);
  }

  playback::KeyValueStore &store_;
  uint8_t level_ = kDefaultLevel;
  bool pendingSave_ = false;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace knobify::power
