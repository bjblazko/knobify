#pragma once

#include "GestureRecognizer.h"

namespace knobify::input {

// Sits between the once-per-loop touch poll and LVGL's periodic indev
// read. LVGL only samples the latest touch state when its read timer
// fires, and loop() iterations take up to ~65ms while Now Playing is
// redrawing (measured on hardware 2026-09-13) -- a quick tap could be
// pressed and released entirely between two reads and never reach LVGL.
// Latching "a press happened since the last read" guarantees every
// polled press is seen as at least one pressed read followed by a
// release, i.e. a click.
class TouchLatch {
 public:
  void feed(const TouchSample &sample) {
    latest_ = sample;
    if (sample.pressed) {
      pressPending_ = true;
      pressX_ = sample.x;
      pressY_ = sample.y;
    }
  }

  TouchSample read() {
    if (latest_.pressed) {
      pressPending_ = false;
      return latest_;
    }
    if (pressPending_) {
      pressPending_ = false;
      return TouchSample{pressX_, pressY_, true};
    }
    return latest_;
  }

 private:
  TouchSample latest_{};
  bool pressPending_ = false;
  int16_t pressX_ = 0;
  int16_t pressY_ = 0;
};

}  // namespace knobify::input
