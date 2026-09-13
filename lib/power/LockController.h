#pragma once

#include <cstdint>

namespace knobify::power {

// Lock state and the hold-button-while-turning unlock gesture (decision,
// ADR 0005). This board's rotary encoder has no push button and there is
// no other software-addressable button (device.md), so unlocking can't
// use a click/long-press on a physical control -- instead, unlocking
// requires holding an on-screen touch button while simultaneously
// rotating the encoder, a two-factor gesture deliberately hard to
// trigger by brushing against fabric in a pocket.
//
// Pure logic, no LVGL/hardware: the caller (main.cpp/LockOverlay) is
// responsible for turning the on-screen unlock button's press/release
// events into onHoldStart()/onHoldEnd() calls, and for rendering
// unlockProgress() as a ring.
class LockController {
 public:
  static constexpr int32_t kUnlockDetentThreshold = 10;

  bool isLocked() const { return locked_; }

  // Called from the Now Playing screen's lock button.
  void requestLock() {
    locked_ = true;
    resetHold();
  }

  // Called when the on-screen unlock button transitions to pressed.
  void onHoldStart(uint32_t nowMs) {
    (void)nowMs;
    holding_ = true;
    accumulatedDetents_ = 0;
  }

  // Called for each encoder delta while the unlock button is held.
  // Ignored if no hold is currently active (button not pressed, or
  // already unlocked).
  void onHoldEncoderDelta(int16_t delta) {
    if (!holding_ || !locked_) return;
    accumulatedDetents_ += delta < 0 ? -delta : delta;
    if (accumulatedDetents_ >= kUnlockDetentThreshold) {
      locked_ = false;
      resetHold();
    }
  }

  // Called when the on-screen unlock button transitions to released.
  // Progress made before completion is discarded immediately -- see
  // ADR 0005: a lingering partial hold would weaken the
  // hard-to-trigger-by-accident guarantee the whole gesture exists for.
  void onHoldEnd() { resetHold(); }

  // 0..1, for driving a progress ring. 0 whenever not actively holding.
  float unlockProgress() const {
    if (!holding_) return 0.0f;
    float progress =
        static_cast<float>(accumulatedDetents_) / kUnlockDetentThreshold;
    return progress < 1.0f ? progress : 1.0f;
  }

 private:
  void resetHold() {
    holding_ = false;
    accumulatedDetents_ = 0;
  }

  bool locked_ = false;
  bool holding_ = false;
  int32_t accumulatedDetents_ = 0;
};

}  // namespace knobify::power
