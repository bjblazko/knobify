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
  // While locked, a dark display only wakes from the knob after about a
  // quarter turn (~30 detents per revolution), so nudges in a pocket
  // don't light the screen and drain the battery. Detents count as a
  // signed sum, so back-and-forth jiggle cancels out, and only while they
  // keep coming: a pause longer than the window starts the count over.
  // Unlocked, none of this applies -- see noteEncoderDelta().
  static constexpr int kEncoderWakeDetents = 8;
  static constexpr uint32_t kEncoderWakeWindowMs = 1500;

  // Call once for every touch sample, regardless of whether it was
  // otherwise consumed/swallowed -- activity itself is what matters here,
  // not what it did.
  void noteActivity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
    displayOn_ = true;
    wakeDetents_ = 0;
  }

  // Call for every non-zero encoder delta. While the display is on, any
  // detent counts as activity. While it's off, `requireDeliberateTurn` is
  // the pocket guard: pass the lock state. Locked, only a deliberate turn
  // (see kEncoderWakeDetents) wakes the display. Unlocked the device is
  // merely dimmed on a table, so the very first detent wakes it -- and
  // the caller acts on that same detent as usual (ADR 0005).
  void noteEncoderDelta(int delta, uint32_t nowMs, bool requireDeliberateTurn) {
    if (displayOn_) {
      noteActivity(nowMs);
      return;
    }
    if (!requireDeliberateTurn) {
      noteActivity(nowMs);
      return;
    }
    if (nowMs - lastEncoderMs_ > kEncoderWakeWindowMs) wakeDetents_ = 0;
    lastEncoderMs_ = nowMs;
    wakeDetents_ += delta;
    if (wakeDetents_ >= kEncoderWakeDetents ||
        wakeDetents_ <= -kEncoderWakeDetents) {
      noteActivity(nowMs);
    }
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
  uint32_t lastEncoderMs_ = 0;
  int wakeDetents_ = 0;
};

}  // namespace knobify::power
