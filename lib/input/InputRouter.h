#pragma once

#include <cstdint>

#include "BrightnessSetting.h"
#include "GestureRecognizer.h"
#include "PlaybackStateMachine.h"
#include "ScreenId.h"
#include "Shuttle.h"
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
// deltas to list/tile selection, volume, the shuttle (while held, ADR 0013) or
// brightness depending on the current screen, and routes the one recognized
// gesture (left-to-right swipe) to TabController's pop-or-switch-tab logic.
// Pure logic over TabController/PlaybackStateMachine/Shuttle/ListMoveSink --
// host-testable, no hardware or LVGL involved.
class InputRouter {
 public:
  InputRouter(navigation::TabController &tabs,
              playback::PlaybackStateMachine &playback,
              playback::Shuttle &shuttle,
              power::BrightnessSetting &brightness, ListMoveSink &listSink)
      : tabs_(tabs),
        playback_(playback),
        shuttle_(shuttle),
        brightness_(brightness),
        listSink_(listSink) {}

  void onEncoderDelta(int16_t delta, uint32_t nowMs) {
    switch (tabs_.activeStack().current().kind) {
      case navigation::ScreenKind::NowPlaying:
        // Holding the time pill turns the knob into a shuttle (ADR 0013);
        // the volume never changes during a hold.
        if (shuttle_.isHeld()) {
          shuttle_.turn(delta, nowMs);
        } else {
          playback_.adjustVolume(delta, nowMs);
        }
        break;
      case navigation::ScreenKind::Brightness:
        brightness_.adjust(delta, nowMs);
        break;
      default:
        // Browse lists and the Home tiles alike.
        listSink_.onListMove(delta);
        break;
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
  playback::Shuttle &shuttle_;
  power::BrightnessSetting &brightness_;
  ListMoveSink &listSink_;
};

}  // namespace knobify::input
