#pragma once

#include <cstdint>

namespace knobify::navigation {

// Which main-menu destinations the user has chosen to show (ADR 0018) --
// one bit per entry in the menu table, 1 = shown.
//
// Pure logic, no storage and no LVGL: ScreenManager reads and writes the
// mask as a single NVS byte, and this decides what the mask means. That
// keeps the two rules worth getting right -- an entry that may never be
// hidden, and never ending up with an empty menu -- host-testable.
class MenuVisibility {
 public:
  static constexpr uint8_t kDefaultMask = 0xFF;  // Everything shown.
  static constexpr int kMaxEntries = 8;          // One mask byte.

  // `pinnedMask` marks entries the user may not hide: Settings, which is
  // the only way back to the screen that does the hiding.
  MenuVisibility(int entryCount, uint8_t pinnedMask)
      : entryCount_(entryCount < 0 ? 0
                    : entryCount > kMaxEntries ? kMaxEntries
                                               : entryCount),
        pinnedMask_(pinnedMask) {}

  uint8_t mask() const { return mask_; }
  void setMask(uint8_t mask) { mask_ = mask; }

  bool pinned(int entryIndex) const {
    return inRange(entryIndex) && (pinnedMask_ & bitFor(entryIndex)) != 0;
  }

  bool visible(int entryIndex) const {
    if (!inRange(entryIndex)) return false;
    return pinned(entryIndex) || (mask_ & bitFor(entryIndex)) != 0;
  }

  int visibleCount() const {
    int count = 0;
    for (int i = 0; i < entryCount_; ++i) {
      if (visible(i)) ++count;
    }
    return count;
  }

  // Why a toggle was refused, so the UI can say which rule it hit rather
  // than leaving the row looking broken.
  enum class ToggleResult { Toggled, Pinned, WouldEmptyMenu };

  ToggleResult toggle(int entryIndex) {
    if (!inRange(entryIndex)) return ToggleResult::Pinned;
    if (pinned(entryIndex)) return ToggleResult::Pinned;
    // Belt and braces next to the pinned rule: a menu with nothing in it
    // would strand the user on a blank Home with no way anywhere.
    if (visible(entryIndex) && visibleCount() <= 1) {
      return ToggleResult::WouldEmptyMenu;
    }
    mask_ ^= bitFor(entryIndex);
    return ToggleResult::Toggled;
  }

 private:
  bool inRange(int entryIndex) const {
    return entryIndex >= 0 && entryIndex < entryCount_;
  }
  // Not named bit(): Arduino.h defines a bit() macro, which would eat any
  // call to it in a translation unit that includes both.
  static uint8_t bitFor(int entryIndex) {
    return static_cast<uint8_t>(1u << entryIndex);
  }

  int entryCount_;
  uint8_t pinnedMask_;
  uint8_t mask_ = kDefaultMask;
};

}  // namespace knobify::navigation
