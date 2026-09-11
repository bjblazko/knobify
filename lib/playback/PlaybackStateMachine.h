#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "PlaybackDriver.h"
#include "VolumePersistence.h"

namespace knobify::playback {

enum class PlaybackState { Stopped, Playing, Paused };

// Playback logic: which track is current, play/pause/next/prev, volume
// (clamped 0-21, debounced-persisted). Pure logic over the PlaybackDriver
// interface -- see docs/adr/0004-navigation-library-and-index-architecture.md
// and docs/adr/0003-testing-strategy.md. Auto-advances within the current
// playlist on TrackFinished; stops after the last track (no repeat in v1).
//
// Callers pass explicit timestamps (millis()-style) to volume methods and
// tick() rather than this class reading a clock itself, so debounced
// persistence is host-testable without real timing.
class PlaybackStateMachine {
 public:
  static constexpr uint8_t kMinVolume = 0;
  static constexpr uint8_t kMaxVolume = 21;
  static constexpr uint32_t kVolumeSaveDebounceMs = 1000;

  PlaybackStateMachine(PlaybackDriver &driver, VolumePersistence &volumeStore)
      : driver_(driver), volumeStore_(volumeStore), volume_(volumeStore.load()) {
    driver_.setVolume(volume_);
  }

  void play(std::vector<std::string> playlist, size_t startIndex) {
    if (startIndex >= playlist.size()) return;
    playlist_ = std::move(playlist);
    index_ = startIndex;
    if (driver_.playFile(playlist_[index_])) {
      state_ = PlaybackState::Playing;
    }
  }

  void togglePlayPause() {
    if (state_ == PlaybackState::Playing) {
      driver_.pause();
      state_ = PlaybackState::Paused;
    } else if (state_ == PlaybackState::Paused) {
      driver_.resume();
      state_ = PlaybackState::Playing;
    }
  }

  void next() { advance(1); }
  void prev() { advance(-1); }

  // Call when the driver reports the current file finished (e.g.
  // isRunning() transitioned true->false while we expected Playing).
  void onTrackFinished() {
    if (state_ != PlaybackState::Playing) return;
    if (index_ + 1 < playlist_.size()) {
      advance(1);
    } else {
      driver_.stop();
      state_ = PlaybackState::Stopped;
    }
  }

  // `delta` is signed knob ticks; positive = louder.
  void adjustVolume(int delta, uint32_t nowMs) {
    int newVolume = static_cast<int>(volume_) + delta;
    if (newVolume < kMinVolume) newVolume = kMinVolume;
    if (newVolume > kMaxVolume) newVolume = kMaxVolume;
    volume_ = static_cast<uint8_t>(newVolume);
    driver_.setVolume(volume_);
    pendingVolumeSave_ = true;
    lastVolumeChangeMs_ = nowMs;
  }

  // Call periodically (e.g. from loop()); persists the volume once it's
  // been stable for kVolumeSaveDebounceMs, avoiding a flash write per
  // knob tick.
  void tick(uint32_t nowMs) {
    if (pendingVolumeSave_ &&
        nowMs - lastVolumeChangeMs_ >= kVolumeSaveDebounceMs) {
      volumeStore_.save(volume_);
      pendingVolumeSave_ = false;
    }
  }

  PlaybackState state() const { return state_; }
  size_t currentIndex() const { return index_; }
  const std::string &currentPath() const { return playlist_[index_]; }
  uint8_t volume() const { return volume_; }
  bool hasPendingVolumeSave() const { return pendingVolumeSave_; }

 private:
  void advance(int direction) {
    if (playlist_.empty()) return;
    long newIndex = static_cast<long>(index_) + direction;
    if (newIndex < 0 || newIndex >= static_cast<long>(playlist_.size())) {
      return;
    }
    index_ = static_cast<size_t>(newIndex);
    if (driver_.playFile(playlist_[index_])) {
      state_ = PlaybackState::Playing;
    }
  }

  PlaybackDriver &driver_;
  VolumePersistence &volumeStore_;
  std::vector<std::string> playlist_;
  size_t index_ = 0;
  PlaybackState state_ = PlaybackState::Stopped;
  uint8_t volume_;
  bool pendingVolumeSave_ = false;
  uint32_t lastVolumeChangeMs_ = 0;
};

}  // namespace knobify::playback
