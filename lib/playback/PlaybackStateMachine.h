#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "AudioBackendKind.h"
#include "PlayQueue.h"
#include "PlaybackDriver.h"
#include "VolumePersistence.h"

namespace knobify::playback {

enum class PlaybackState { Stopped, Playing, Paused };

// Playback logic: which track is current, play/pause/next/prev, volume
// (clamped 0-21, debounced-persisted), and elapsed play time. Pure logic
// over the PlaybackDriver interface -- see
// docs/adr/0004-navigation-library-and-index-architecture.md and
// docs/adr/0003-testing-strategy.md. Auto-advances through the PlayQueue
// on TrackFinished, honoring shuffle and repeat (ADR 0011); with repeat
// off it stops after the last track.
//
// Callers pass explicit timestamps (millis()-style) rather than this
// class reading a clock itself, so debounced volume persistence and
// elapsed-time tracking are both host-testable without real timing.
// Elapsed time is wall-clock time since the current track started minus
// time spent paused -- not the decoder's actual playback position (the
// PlaybackDriver interface doesn't expose one) -- close enough for a
// simple on-screen readout, but it will drift from the real position if
// the codec itself stalls or buffers.
//
// The constructor deliberately does NOT touch the driver or volume
// store -- when this object is a global (as it is in src/main.cpp),
// its constructor runs during C++ static initialization, before
// Arduino's runtime has initialized NVS or called any driver's begin().
// Call begin() explicitly from setup(), once both are actually ready.
class PlaybackStateMachine {
 public:
  static constexpr uint8_t kMinVolume = 0;
  static constexpr uint8_t kMaxVolume = 21;
  static constexpr uint32_t kVolumeSaveDebounceMs = 1000;

  PlaybackStateMachine(PlaybackDriver &driver, VolumePersistence &volumeStore)
      : driver_(driver), volumeStore_(volumeStore) {}

  // Loads the persisted volume and applies it to the driver. Must be
  // called once, after the driver's own begin() has run.
  void begin() {
    volume_ = volumeStore_.load();
    driver_.setVolume(volume_);
  }

  // Scales the output by 0..4096 (unity) without changing volume() or
  // saving anything -- the sleep timer's fade (ADR 0015), so a faded-out
  // volume is never what comes back after waking.
  void setOutputGain(uint16_t gain) { driver_.setOutputGain(gain); }

  // Replaces the queue. `shuffle` plays the whole playlist in random order
  // (startIndex is then ignored); without it shuffle is switched off, so a
  // tapped track always plays its list in order.
  // `scope` only records what the queue came from (messages, resume).
  void play(std::vector<std::string> playlist, size_t startIndex,
            uint32_t nowMs, bool shuffle = false,
            PlayScope scope = PlayScope::File) {
    if (startIndex >= playlist.size()) return;
    loadQueue(std::move(playlist), startIndex, shuffle, scope);
    playCurrent(nowMs);
  }

  // Restores a queue after a reboot without playing it (ADR 0012): Paused
  // at `elapsedSeconds`, with nothing loaded in the driver yet. The next
  // play/pause starts the track at `filePosition`. The track is at
  // `startIndex` in the original order; with `shuffle` the rest of the
  // queue is shuffled after it (the old shuffled order isn't kept).
  void cue(std::vector<std::string> playlist, size_t startIndex, bool shuffle,
           PlayScope scope, uint32_t filePosition, uint32_t elapsedSeconds,
           uint32_t nowMs) {
    if (startIndex >= playlist.size()) return;
    loadQueue(std::move(playlist), startIndex, false, scope);
    queue_.setShuffled(shuffle);
    state_ = PlaybackState::Paused;
    cued_ = true;
    cuedFilePosition_ = filePosition;
    trackStartMs_ = nowMs - elapsedSeconds * 1000u;
    pausedAccumMs_ = 0;
    pauseStartMs_ = nowMs;
  }

  // Seeds shuffling; call once from setup() with a hardware random value,
  // or every boot shuffles the same way.
  void setRandomSeed(uint32_t seed) { seed_ = seed; }

  // Reorders the running queue without interrupting the current track.
  void setShuffle(bool shuffle) { queue_.setShuffled(shuffle); }
  bool shuffle() const { return queue_.shuffled(); }

  void setRepeat(RepeatMode repeat) { repeat_ = repeat; }
  RepeatMode repeat() const { return repeat_; }
  void cycleRepeat() {
    repeat_ = static_cast<RepeatMode>((static_cast<uint8_t>(repeat_) + 1) % 3);
  }

  void togglePlayPause(uint32_t nowMs) {
    if (cued_) {
      if (driver_.playFileAt(queue_.current(), cuedFilePosition_)) {
        cued_ = false;
        state_ = PlaybackState::Playing;
        pausedAccumMs_ += nowMs - pauseStartMs_;
      }
      return;
    }
    if (state_ == PlaybackState::Playing) {
      driver_.pause();
      state_ = PlaybackState::Paused;
      pauseStartMs_ = nowMs;
    } else if (state_ == PlaybackState::Paused) {
      driver_.resume();
      state_ = PlaybackState::Playing;
      pausedAccumMs_ += nowMs - pauseStartMs_;
    }
  }

  void next(uint32_t nowMs) {
    if (queue_.next(repeat_)) playCurrent(nowMs);
  }
  void prev(uint32_t nowMs) {
    if (queue_.prev(repeat_)) playCurrent(nowMs);
  }

  // Call when the driver reports the current file finished (e.g.
  // isRunning() transitioned true->false while we expected Playing).
  void onTrackFinished(uint32_t nowMs) {
    if (state_ != PlaybackState::Playing) return;
    if (queue_.onFinished(repeat_)) {
      playCurrent(nowMs);
    } else {
      driver_.stop();
      state_ = PlaybackState::Stopped;
    }
  }

  // Stops and closes the current file, keeping the queue -- for handing the
  // SD card to a computer (ADR 0016).
  void stop() {
    if (state_ == PlaybackState::Stopped && !cued_) return;
    driver_.stop();
    state_ = PlaybackState::Stopped;
    cued_ = false;
  }

  // Moves within the current track (jog/shuttle, ADR 0013) and shifts the
  // wall-clock elapsed time by the same amount, so the readout and ring
  // follow. Clamped at the track start; the end is the caller's job (it
  // knows its safety margin). Nothing loaded (stopped, or cued after a
  // reboot) -> no-op.
  void seekBy(int32_t deltaMs, uint32_t nowMs) {
    if (state_ == PlaybackState::Stopped || cued_) return;
    int64_t elapsed = elapsedMs(nowMs);
    if (elapsed + deltaMs < 0) deltaMs = static_cast<int32_t>(-elapsed);
    if (deltaMs == 0) return;
    if (!driver_.seekByMs(deltaMs)) return;
    // Modular arithmetic: subtracting a negative delta moves the start
    // later, i.e. less elapsed.
    trackStartMs_ -= static_cast<uint32_t>(deltaMs);
  }

  // Whether the current track can be shuttled. backendForPath() (owned by
  // AudioBackendKind.h) is the one place that knows which decoder a path
  // goes to; everything on knobify's own Vorbis path seeks by sample
  // (ADR 0017), so it's always seekable, and this only needs to name the
  // library codecs that seek (MP3, M4A, WAV) among the rest of the
  // library path (e.g. FLAC, which doesn't). Known before a cued track
  // loads, from the path alone.
  bool canSeek() const {
    if (state_ == PlaybackState::Stopped || queue_.empty()) return false;
    const std::string &path = queue_.current();
    if (backendForPath(path) == AudioBackendKind::Vorbis) return true;
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "mp3" || ext == "m4a" || ext == "wav";
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
  bool hasQueue() const { return !queue_.empty(); }
  PlayScope scope() const { return scope_; }
  // The queue in original order; meaningless without hasQueue().
  const std::vector<std::string> &playlist() const { return queue_.tracks(); }
  // Where the current track would resume, for persisting (ADR 0012).
  uint32_t filePosition() const {
    if (cued_) return cuedFilePosition_;
    if (state_ == PlaybackState::Stopped) return 0;
    return driver_.filePosition();
  }
  // Position in play order (shuffled order while shuffle is on).
  size_t currentIndex() const { return queue_.position(); }
  const std::string &currentPath() const { return queue_.current(); }
  // Bumped every time playCurrent() actually starts a file -- play(),
  // next()/prev(), and an onTrackFinished() restart/advance -- so callers
  // like Shuttle (ADR 0013) can tell a track restart (e.g. repeat-one, or
  // a one-track queue on repeat-all) apart from merely resuming the same
  // track, which currentPath() alone can't do. Resuming a cued track via
  // togglePlayPause() does NOT bump this: it's the same track starting to
  // actually play, not a change.
  uint32_t trackGeneration() const { return trackGeneration_; }
  uint8_t volume() const { return volume_; }
  bool hasPendingVolumeSave() const { return pendingVolumeSave_; }

  // Current track's duration in seconds, 0 if unknown (see
  // PlaybackDriver::durationSeconds()).
  uint32_t durationSeconds() const {
    if (state_ == PlaybackState::Stopped) return 0;
    return driver_.durationSeconds();
  }

  // Recent DAC samples for the spectrum analyzer; nothing unless actually
  // playing, so a paused track's last buffer never looks live.
  SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples) {
    if (state_ != PlaybackState::Playing) return {};
    return driver_.readRecentSamples(dst, maxSamples);
  }

  // Milliseconds of actual playback since the current track started,
  // excluding time spent paused. 0 when stopped.
  uint32_t elapsedMs(uint32_t nowMs) const {
    if (state_ == PlaybackState::Stopped) return 0;
    uint32_t pausedSoFar = pausedAccumMs_;
    if (state_ == PlaybackState::Paused) {
      pausedSoFar = static_cast<uint32_t>(pausedSoFar + (nowMs - pauseStartMs_));
    }
    return static_cast<uint32_t>(nowMs - trackStartMs_ - pausedSoFar);
  }

 private:
  void loadQueue(std::vector<std::string> playlist, size_t startIndex,
                 bool shuffle, PlayScope scope) {
    // A different permutation per shuffle, from one seed.
    seed_ = seed_ * 1664525u + 1013904223u;
    queue_.load(std::move(playlist), startIndex, shuffle, seed_);
    scope_ = scope;
  }

  void playCurrent(uint32_t nowMs) {
    // A skip while cued leaves the cued position behind: a failed start
    // must not later resume the new track mid-way, or show the old time.
    if (cued_) {
      cuedFilePosition_ = 0;
      trackStartMs_ = nowMs;
      pausedAccumMs_ = 0;
      pauseStartMs_ = nowMs;
    }
    if (driver_.playFile(queue_.current())) {
      cued_ = false;
      state_ = PlaybackState::Playing;
      trackStartMs_ = nowMs;
      pausedAccumMs_ = 0;
      ++trackGeneration_;
      return;
    }
    // A track that cannot start leaves the driver not running, which the
    // main loop reads as "finished" and would advance again -- through the
    // whole queue at one track per second. Stop instead.
    driver_.stop();
    state_ = PlaybackState::Stopped;
    cued_ = false;
  }

  PlaybackDriver &driver_;
  VolumePersistence &volumeStore_;
  PlayQueue queue_;
  RepeatMode repeat_ = RepeatMode::Off;
  PlayScope scope_ = PlayScope::File;
  // Restored but not yet loaded into the driver -- see cue().
  bool cued_ = false;
  uint32_t cuedFilePosition_ = 0;
  uint32_t seed_ = 1;
  PlaybackState state_ = PlaybackState::Stopped;
  uint8_t volume_ = 0;
  bool pendingVolumeSave_ = false;
  uint32_t lastVolumeChangeMs_ = 0;
  uint32_t trackStartMs_ = 0;
  uint32_t pausedAccumMs_ = 0;
  uint32_t pauseStartMs_ = 0;
  uint32_t trackGeneration_ = 0;
};

}  // namespace knobify::playback
