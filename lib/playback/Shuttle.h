#pragma once

#include <cstdint>
#include <cstdlib>

#include "PlaybackStateMachine.h"

namespace knobify::playback {

// Jog/shuttle on Now Playing (ADR 0013): while the time pill is held, knob
// detents set a speed step (±kMaxStep, speed 1 << |step|) and the track is
// cued CD-style -- a kCycleMs snippet at normal speed, then a jump sized so
// the overall rate matches the step. Pure logic over PlaybackStateMachine,
// host-testable with explicit timestamps like everything in lib/playback.
//
// End stops are real pauses ("parked"): the state machine then reports
// Paused, so elapsed time freezes and main.cpp's track-finished detection
// (which only runs while Playing) can't fire and skip to the next track.
class Shuttle {
 public:
  static constexpr int8_t kMaxStep = 5;
  static constexpr uint32_t kCycleMs = 600;
  // Forward parks this far before the end, so the track can't finish
  // while held.
  static constexpr uint32_t kEndMarginMs = 1000;

  explicit Shuttle(PlaybackStateMachine &playback) : playback_(playback) {}

  // Starts a hold; false (and nothing held) for a track that can't seek.
  // Cueing is audible even from pause, so a paused track plays for the
  // hold's duration.
  bool hold(uint32_t nowMs) {
    if (held_) return true;
    if (!playback_.canSeek()) return false;
    held_ = true;
    step_ = 0;
    parked_ = false;
    trackGeneration_ = playback_.trackGeneration();
    wasPaused_ = playback_.state() == PlaybackState::Paused;
    if (wasPaused_) playback_.togglePlayPause(nowMs);
    cycleStartMs_ = nowMs;
    return true;
  }

  // Ends a hold: the position stays, play/pause returns to what it was.
  void release(uint32_t nowMs) {
    if (!held_) return;
    bool playing = playback_.state() == PlaybackState::Playing;
    if (wasPaused_ == playing) playback_.togglePlayPause(nowMs);
    drop();
  }

  // `delta` in detents; positive = forward.
  void turn(int delta, uint32_t) {
    if (!held_) return;
    int step = step_ + delta;
    if (step > kMaxStep) step = kMaxStep;
    if (step < -kMaxStep) step = -kMaxStep;
    step_ = static_cast<int8_t>(step);
  }

  // Call every loop() iteration.
  void tick(uint32_t nowMs) {
    if (!held_) return;
    // Skipped or finished into another track: that's the buttons' job,
    // and the old hold means nothing for the new track. Compared by
    // trackGeneration(), not currentPath() -- a repeat-one restart (or a
    // one-track queue on repeat-all) keeps the same path but is still a
    // new track start.
    if (playback_.state() == PlaybackState::Stopped ||
        playback_.trackGeneration() != trackGeneration_) {
      drop();
      return;
    }
    if (parked_) {
      bool stillPushing = parkedAtEnd_ ? step_ > 0 : step_ < 0;
      if (stillPushing) return;
      parked_ = false;
      playback_.togglePlayPause(nowMs);
      cycleStartMs_ = nowMs;
      return;
    }
    if (step_ == 0) {
      cycleStartMs_ = nowMs;
      return;
    }
    if (nowMs - cycleStartMs_ < kCycleMs) return;
    cycleStartMs_ = nowMs;

    uint32_t durationMs = playback_.durationSeconds() * 1000u;
    if (durationMs <= kEndMarginMs) return;  // Unknown (not parsed yet) or tiny.
    int64_t speed = int64_t{1} << std::abs(step_);
    int64_t jump = step_ > 0 ? (speed - 1) * kCycleMs : -(speed + 1) * kCycleMs;
    int64_t position = playback_.elapsedMs(nowMs);
    int64_t endStop = durationMs - kEndMarginMs;
    int64_t target = position + jump;
    bool park = false;
    if (target >= endStop) {
      target = endStop;
      park = true;
      parkedAtEnd_ = true;
    } else if (target <= 0) {
      target = 0;
      park = true;
      parkedAtEnd_ = false;
    }
    playback_.seekBy(static_cast<int32_t>(target - position), nowMs);
    if (park) {
      parked_ = true;
      if (playback_.state() == PlaybackState::Playing) {
        playback_.togglePlayPause(nowMs);
      }
    }
  }

  bool isHeld() const { return held_; }
  int8_t step() const { return step_; }

 private:
  void drop() {
    held_ = false;
    step_ = 0;
    parked_ = false;
  }

  PlaybackStateMachine &playback_;
  bool held_ = false;
  int8_t step_ = 0;
  bool wasPaused_ = false;
  bool parked_ = false;
  bool parkedAtEnd_ = false;
  uint32_t cycleStartMs_ = 0;
  uint32_t trackGeneration_ = 0;
};

}  // namespace knobify::playback
