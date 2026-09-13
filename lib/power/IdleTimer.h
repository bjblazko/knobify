#pragma once

#include <cstdint>

namespace knobify::power {

// Tracks display on/off based on touch/encoder activity, independent of
// lock state (decision, ADR 0005) -- a device sitting on a table should
// dim after a while without ever being "locked". Pure logic over an
// explicit clock argument (matching PlaybackStateMachine's pattern) so
// it's host-testable without a real timer; the caller (main.cpp) is
// responsible for actually driving the backlight
// (St77916Driver::setBacklight) based on isDisplayOn().
class IdleTimer {
 public:
  static constexpr uint32_t kIdleTimeoutMs = 60000;

  // Call once for every touch or encoder sample, regardless of whether it
  // was otherwise consumed/swallowed -- activity itself is what matters
  // here, not what it did.
  void noteActivity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
    displayOn_ = true;
  }

  // Call periodically (e.g. once per loop()). Returns the current
  // "display should be on" state after applying the idle timeout.
  bool tick(uint32_t nowMs) {
    if (displayOn_ && nowMs - lastActivityMs_ >= kIdleTimeoutMs) {
      displayOn_ = false;
    }
    return displayOn_;
  }

  bool isDisplayOn() const { return displayOn_; }

 private:
  bool displayOn_ = true;
  uint32_t lastActivityMs_ = 0;
};

}  // namespace knobify::power
