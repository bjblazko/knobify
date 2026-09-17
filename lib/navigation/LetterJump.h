#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace knobify::navigation {

// One initial-letter group in a browse list: `letter` is what the header
// shows, `row` the first list row belonging to it.
struct LetterBucket {
  char letter;
  int row;
};

// Jump-by-letter on long lists (ADR 0021). Tapping the context caption
// opens this mode; each detent then moves the highlight to the next
// letter group instead of the next row, and the mode closes itself after
// kIdleCloseMs without a turn.
//
// Buckets are letter *boundaries*, not a fixed A-Z index: a bucket starts
// wherever a row's letter differs from the row above it. A Files listing
// (folders A..Z, then files A..Z) therefore works without a special case
// -- you simply step through the boundaries in list order.
//
// Pure logic with explicit timestamps, like NavigationStack and Shuttle:
// no LVGL, no hardware, host-testable. Callers pass keys already folded
// the way their list is sorted (LibraryIndex::nameSortKey() /
// foldAccents()), so the letter can never disagree with the row order.
class LetterJump {
 public:
  // Below this, turning row by row is quicker than reading a letter.
  static constexpr size_t kMinRowsToOffer = 20;
  // Between turns, once a hand is already on the knob.
  static constexpr uint32_t kIdleCloseMs = 1500;
  // Before the first turn. The mode is opened by a finger on the glass
  // and used by a hand on the knob, and moving from one to the other
  // takes longer than the between-turns timeout -- measured on the
  // device 2026-09-17, where every single tap timed out before its turn
  // arrived and the mode looked dead.
  static constexpr uint32_t kFirstTurnMs = 5000;

  // The letter a sort key belongs under: uppercased ASCII, everything
  // else (digits, symbols, non-Latin scripts) collapsed into one group.
  static char letterFor(const std::string &key) {
    if (key.empty()) return '#';
    const unsigned char c = static_cast<unsigned char>(key[0]);
    if (c > 0x7F || std::isalpha(c) == 0) return '#';
    return static_cast<char>(std::toupper(c));
  }

  // `keys[i]` is the sort key of list row `firstRow + i` -- leading
  // action rows (Shuffle, Continue) are simply not passed in.
  static std::vector<LetterBucket> buckets(const std::vector<std::string> &keys,
                                           int firstRow) {
    std::vector<LetterBucket> result;
    char previous = '\0';
    for (size_t i = 0; i < keys.size(); ++i) {
      const char letter = letterFor(keys[i]);
      if (letter == previous) continue;
      previous = letter;
      result.push_back(LetterBucket{letter, firstRow + static_cast<int>(i)});
    }
    return result;
  }

  // Whether the caption should offer the mode at all: enough rows to be
  // worth it, and more than one letter to move between.
  static bool eligible(const std::vector<LetterBucket> &buckets,
                       size_t rowCount) {
    return buckets.size() >= 2 && rowCount >= kMinRowsToOffer;
  }

  void setBuckets(std::vector<LetterBucket> buckets) {
    buckets_ = std::move(buckets);
    active_ = false;
  }

  const std::vector<LetterBucket> &buckets() const { return buckets_; }

  bool eligible(size_t rowCount) const { return eligible(buckets_, rowCount); }

  // Starts at the bucket `currentRow` sits in, so the first detent moves
  // one letter from where you already are rather than back to 'A'.
  void open(int currentRow, uint32_t nowMs) {
    if (buckets_.empty()) return;
    active_ = true;
    lastTurnMs_ = nowMs;
    turned_ = false;
    index_ = 0;
    for (size_t i = 0; i < buckets_.size(); ++i) {
      if (buckets_[i].row <= currentRow) index_ = i;
    }
  }

  void close() { active_ = false; }

  bool active() const { return active_; }

  char letter() const {
    if (!active_ || buckets_.empty()) return '\0';
    return buckets_[index_].letter;
  }

  // Returns the row to highlight. Clamps at both ends rather than
  // wrapping: running past Z and landing back on A reads as a glitch.
  int turn(int delta, uint32_t nowMs) {
    if (!active_ || buckets_.empty()) return -1;
    lastTurnMs_ = nowMs;
    turned_ = true;
    int next = static_cast<int>(index_) + delta;
    if (next < 0) next = 0;
    if (next > static_cast<int>(buckets_.size()) - 1) {
      next = static_cast<int>(buckets_.size()) - 1;
    }
    index_ = static_cast<size_t>(next);
    return buckets_[index_].row;
  }

  // Call every loop: closes the mode once it has been left alone.
  void tick(uint32_t nowMs) {
    if (!active_) return;
    const uint32_t limit = turned_ ? kIdleCloseMs : kFirstTurnMs;
    if (nowMs - lastTurnMs_ >= limit) active_ = false;
  }

 private:
  std::vector<LetterBucket> buckets_;
  size_t index_ = 0;
  bool active_ = false;
  bool turned_ = false;
  uint32_t lastTurnMs_ = 0;
};

}  // namespace knobify::navigation
