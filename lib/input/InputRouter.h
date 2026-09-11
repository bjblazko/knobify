#pragma once

#include <cstdint>

#include "GestureRecognizer.h"
#include "PlaybackStateMachine.h"
#include "ScreenId.h"
#include "TabController.h"

namespace knobify::input {

// Consumes encoder deltas and lists don't own their own scrolling here --
// this sink just forwards "move the highlight by this many steps" to
// whichever list UI is currently showing (lib/ui/), so InputRouter
// doesn't need to know about LVGL widgets.
class ListMoveSink {
 public:
  virtual ~ListMoveSink() = default;
  virtual void onListMove(int16_t delta) = 0;
};

// The context-sensitive piece (decision 3, ADR 0004): routes encoder
// deltas to either list-scrolling or volume depending on the current
// screen, and routes the one recognized gesture (left-to-right swipe) to
// TabController's pop-or-switch-tab logic. Pure logic over
// TabController/PlaybackStateMachine/ListMoveSink -- host-testable, no
// hardware or LVGL involved.
class InputRouter {
 public:
  InputRouter(navigation::TabController &tabs,
              playback::PlaybackStateMachine &playback, ListMoveSink &listSink)
      : tabs_(tabs), playback_(playback), listSink_(listSink) {}

  void onEncoderDelta(int16_t delta, uint32_t nowMs) {
    if (tabs_.activeStack().current().kind ==
        navigation::ScreenKind::NowPlaying) {
      playback_.adjustVolume(delta, nowMs);
    } else {
      listSink_.onListMove(delta);
    }
  }

  void onGesture(const GestureEvent &event) {
    if (event.type == GestureType::SwipeLeftToRight) {
      tabs_.handleSwipeBack();
    }
    // Tap is resolved directly by the UI layer's own hit-testing, not
    // routed here.
  }

 private:
  navigation::TabController &tabs_;
  playback::PlaybackStateMachine &playback_;
  ListMoveSink &listSink_;
};

}  // namespace knobify::input
