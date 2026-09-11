#pragma once

#include <cstdint>
#include <optional>

namespace knobify::input {

struct TouchSample {
  int16_t x;
  int16_t y;
  bool pressed;  // true while finger is down; a false sample is the "up".
};

enum class GestureType { Tap, SwipeLeftToRight };

struct GestureEvent {
  GestureType type;
  int16_t x;
  int16_t y;
};

// Turns a stream of raw touch samples into a Tap or a left-to-right
// swipe (the only gesture v1 uses -- see decision 5, ADR 0004). A drag
// that's neither a small enough movement (tap) nor a clean-enough
// horizontal swipe (e.g. too much vertical drift, or right-to-left)
// yields no event at all -- it's simply not one of the gestures this app
// recognizes.
class GestureRecognizer {
 public:
  static constexpr int16_t kTapMaxMovement = 10;
  static constexpr int16_t kSwipeMinDx = 40;
  static constexpr int16_t kSwipeMaxDy = 30;

  // Feed samples in order; an event is produced on the "up" sample (the
  // first `pressed == false` after a down), if the gesture is recognized.
  std::optional<GestureEvent> feed(const TouchSample &sample) {
    lastX_ = sample.x;
    lastY_ = sample.y;

    if (sample.pressed) {
      if (!down_) {
        down_ = true;
        downX_ = sample.x;
        downY_ = sample.y;
      }
      return std::nullopt;
    }

    if (!down_) {
      return std::nullopt;  // Spurious "up" with no preceding "down".
    }
    down_ = false;

    int16_t dx = lastX_ - downX_;
    int16_t dy = lastY_ - downY_;
    int16_t absDx = dx < 0 ? -dx : dx;
    int16_t absDy = dy < 0 ? -dy : dy;

    if (absDx <= kTapMaxMovement && absDy <= kTapMaxMovement) {
      return GestureEvent{GestureType::Tap, lastX_, lastY_};
    }
    if (dx >= kSwipeMinDx && absDy <= kSwipeMaxDy) {
      return GestureEvent{GestureType::SwipeLeftToRight, lastX_, lastY_};
    }
    return std::nullopt;
  }

 private:
  bool down_ = false;
  int16_t downX_ = 0;
  int16_t downY_ = 0;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
};

}  // namespace knobify::input
