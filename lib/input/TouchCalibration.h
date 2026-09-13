#pragma once

#include "GestureRecognizer.h"

namespace knobify::input {

// Maps the CST816's raw coordinates onto the 360x360 display. On this
// board the panel's X axis doesn't line up with the display: raw X is
// scaled and shifted (raw ~= 1.18 * visual - 66), so every tap reported
// 20-50px left of the finger -- buttons narrower than that offset (prev,
// play, lock, back, scan) mostly missed, while full-width list rows and
// the right-hand next button still worked. Fitted from crosshair taps at
// x=90/180/270 plus real button taps, captured over serial on hardware
// 2026-09-13 (residual within ~10px). Y matched within a few px, so it's
// passed through.
class TouchCalibration {
 public:
  static constexpr int kXScaleMilli = 1183;
  static constexpr int kXOffset = -66;
  static constexpr int16_t kMaxCoord = 359;

  static TouchSample apply(const TouchSample &raw) {
    int x = (static_cast<int>(raw.x) - kXOffset) * 1000 / kXScaleMilli;
    return TouchSample{clamp(x), clamp(raw.y), raw.pressed};
  }

 private:
  static int16_t clamp(int v) {
    if (v < 0) return 0;
    if (v > kMaxCoord) return kMaxCoord;
    return static_cast<int16_t>(v);
  }
};

}  // namespace knobify::input
