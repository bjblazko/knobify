#pragma once

#include <cstdint>

#include "GeneratorControl.h"
#include "KeyValueStore.h"
#include "ToneSettings.h"

namespace knobify::signal {

// One tone generator in use (ADR 0024): the settings, whether it sounds,
// and where the sound goes. Every change reaches the output at once;
// persisting waits until the knob has rested, so a sweep across the band
// is not a hundred flash writes. Pure logic -- pausing any music first is
// the screen's job, since only it holds the player.
class ToneSession {
 public:
  static constexpr uint32_t kSaveDebounceMs = 2000;

  ToneSession(GeneratorOutput &output, playback::KeyValueStore &store)
      : output_(output), store_(store) {}

  void begin() {
    settings_.load(store_);
    output_.apply(settings_.params());
  }

  const ToneSettings &settings() const { return settings_; }
  void select(ToneParam param) { settings_.select(param); }

  bool turn(int delta, uint32_t nowMs) {
    if (!settings_.turn(delta, nowMs)) return false;
    output_.apply(settings_.params());
    pendingSave_ = true;
    lastChangeMs_ = nowMs;
    return true;
  }

  void start() {
    if (running_) return;
    output_.apply(settings_.params());
    output_.start();
    running_ = true;
  }

  // Also saves anything still waiting: stopping is usually leaving.
  void stop() {
    if (running_) {
      output_.stop();
      running_ = false;
    }
    flush();
  }

  bool running() const { return running_; }

  void tick(uint32_t nowMs) {
    if (pendingSave_ && nowMs - lastChangeMs_ >= kSaveDebounceMs) flush();
  }

 private:
  void flush() {
    if (!pendingSave_) return;
    settings_.save(store_);
    pendingSave_ = false;
  }

  GeneratorOutput &output_;
  playback::KeyValueStore &store_;
  ToneSettings settings_;
  bool running_ = false;
  bool pendingSave_ = false;
  uint32_t lastChangeMs_ = 0;
};

}  // namespace knobify::signal
