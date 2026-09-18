#pragma once

#include <cstdint>

#include "BrightnessSetting.h"
#include "GestureRecognizer.h"
#include "PlaybackStateMachine.h"
#include "ScreenId.h"
#include "Shuttle.h"
#include "SleepTimer.h"
#include "TabController.h"
#include "ToneSession.h"
#include "TouchCalibrator.h"

namespace knobify::input {

// What the knob does on a screen that reads it directly, forwarded to
// whichever UI is currently showing (lib/ui/) so InputRouter never needs
// to know about LVGL widgets. Screens that give the knob a meaning of
// their own -- volume, brightness, the sleep timer -- are handled inside
// InputRouter instead, because those targets are plain logic objects it
// already holds.
class KnobSink {
 public:
  virtual ~KnobSink() = default;
  // Move the highlight by this many steps: browse lists and Home tiles.
  virtual void onListMove(int16_t delta) = 0;
  // The knob on a game screen, which reads it directly: a paddle in Table
  // Tennis, the craft's heading in Gravity. One method rather than one per
  // game -- the sink would otherwise grow a name for every game added, and
  // only the screen showing knows what a detent means there.
  virtual void onGameKnob(int16_t delta) = 0;
};

// The context-sensitive piece (decision 3, ADR 0004): routes encoder
// deltas to list/tile selection, volume, the shuttle (while held, ADR 0013),
// brightness, the sleep timer or cancelling touch calibration depending on
// the current screen, and routes the one recognized gesture (left-to-right
// swipe) to TabController's pop-or-switch-tab logic.
// Pure logic over TabController/PlaybackStateMachine/Shuttle/KnobSink --
// host-testable, no hardware or LVGL involved.
class InputRouter {
 public:
  InputRouter(navigation::TabController &tabs,
              playback::PlaybackStateMachine &playback,
              playback::Shuttle &shuttle,
              power::BrightnessSetting &brightness,
              power::SleepTimer &sleepTimer,
              TouchCalibrationFlow &calibration, KnobSink &knobSink)
      : tabs_(tabs),
        playback_(playback),
        shuttle_(shuttle),
        brightness_(brightness),
        sleepTimer_(sleepTimer),
        calibration_(calibration),
        knobSink_(knobSink) {}

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
      case navigation::ScreenKind::SleepTimer:
        sleepTimer_.step(delta, nowMs);
        break;
      case navigation::ScreenKind::TableTennis:
      case navigation::ScreenKind::Gravity:
        // The knob is the controller both originals were played with: an
        // Atari paddle is a potentiometer (ADR 0022), and a lander turns
        // (ADR 0023).
        knobSink_.onGameKnob(delta);
        break;
      case navigation::ScreenKind::ToneGenerator:
        // Whichever chip is selected (ADR 0024). Held here like brightness:
        // it is plain logic, and loop() only needs to redraw afterwards.
        if (toneSession_) toneSession_->turn(delta, nowMs);
        break;
      case navigation::ScreenKind::TouchCalibration:
        // The way out that never depends on touch: restores the previous
        // calibration and leaves (TouchCalibrator.h).
        calibration_.cancel();
        tabs_.back();
        break;
      default:
        // Browse lists and the Home tiles alike.
        knobSink_.onListMove(delta);
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

  // Optional, like a game's sounds: without one the knob does nothing on
  // the tone generator, which is all a host-side build needs.
  void setToneSession(signal::ToneSession &session) { toneSession_ = &session; }

 private:
  navigation::TabController &tabs_;
  playback::PlaybackStateMachine &playback_;
  playback::Shuttle &shuttle_;
  power::BrightnessSetting &brightness_;
  power::SleepTimer &sleepTimer_;
  TouchCalibrationFlow &calibration_;
  KnobSink &knobSink_;
  signal::ToneSession *toneSession_ = nullptr;
};

}  // namespace knobify::input
