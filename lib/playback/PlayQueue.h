#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace knobify::playback {

// Off -> All -> One, the order the Now Playing repeat toggle cycles through.
enum class RepeatMode : uint8_t { Off = 0, All = 1, One = 2 };

// The tracks PlaybackStateMachine plays, in original or shuffled order --
// see docs/adr/0011-shuffle-and-repeat.md. Pure logic, no driver: each
// move returns whether there is a track to play and leaves it in current().
//
// The original order is kept alongside the play order, so switching
// shuffle off continues after the current track as if it had never been on.
class PlayQueue {
 public:
  // `shuffled` plays the whole list in random order and ignores
  // `startIndex` -- a Shuffle row has no track to start from. `seed` feeds
  // the shuffle (esp_random() on the device, fixed in tests).
  void load(std::vector<std::string> tracks, size_t startIndex, bool shuffled,
            uint32_t seed) {
    tracks_ = std::move(tracks);
    rng_ = seed != 0 ? seed : 1;  // xorshift gets stuck at 0.
    order_.resize(tracks_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    pos_ = startIndex < tracks_.size() ? startIndex : 0;
    shuffled_ = shuffled;
    if (shuffled_) {
      shuffleFrom(0);
      pos_ = 0;
    }
  }

  // On: the current track stays and becomes first, the rest are shuffled
  // after it. Off: back to the original order at the current track.
  void setShuffled(bool shuffled) {
    if (shuffled == shuffled_ || tracks_.empty()) {
      shuffled_ = shuffled;
      return;
    }
    size_t current = order_[pos_];
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    if (shuffled) {
      std::swap(order_[0], order_[current]);
      shuffleFrom(1);
      pos_ = 0;
    } else {
      pos_ = current;
    }
    shuffled_ = shuffled;
  }

  // Manual skips. Repeat One skips like All -- only a finished track repeats.
  bool next(RepeatMode repeat) { return step(1, repeat != RepeatMode::Off); }
  bool prev(RepeatMode repeat) { return step(-1, repeat != RepeatMode::Off); }

  // The current track played to its end.
  bool onFinished(RepeatMode repeat) {
    if (tracks_.empty()) return false;
    if (repeat == RepeatMode::One) return true;
    return step(1, repeat == RepeatMode::All);
  }

  bool empty() const { return tracks_.empty(); }
  bool shuffled() const { return shuffled_; }
  const std::string &current() const { return tracks_[order_[pos_]]; }
  // Position in play order, not in the original list.
  size_t position() const { return pos_; }

 private:
  bool step(int direction, bool wrap) {
    if (tracks_.empty()) return false;
    long next = static_cast<long>(pos_) + direction;
    long size = static_cast<long>(order_.size());
    if (next < 0 || next >= size) {
      if (!wrap) return false;
      next = (next + size) % size;
    }
    pos_ = static_cast<size_t>(next);
    return true;
  }

  // Fisher-Yates over order_[first..].
  void shuffleFrom(size_t first) {
    for (size_t i = order_.size(); i > first + 1; --i) {
      size_t j = first + random() % (i - first);
      std::swap(order_[i - 1], order_[j]);
    }
  }

  uint32_t random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
  }

  std::vector<std::string> tracks_;
  std::vector<size_t> order_;
  size_t pos_ = 0;
  bool shuffled_ = false;
  uint32_t rng_ = 1;
};

}  // namespace knobify::playback
