#pragma once

#include <cstdint>

namespace knobify::games {

struct TerrainPoint {
  int16_t x;
  int16_t y;
};

// A flat stretch worth landing on. The narrower it is, the more it pays,
// which is what the 1979 machine does and what makes choosing one a
// decision rather than a formality.
struct Pad {
  int16_t left;
  int16_t right;
  int16_t y;
  uint8_t multiplier;
};

// Gravity's landscape (ADR 0023): a closed profile across the world, with
// a few stretches flattened into landing pads.
//
// Pure logic, no LVGL: the screen layer copies profile() straight into an
// lv_line's points. Generation is a seeded LCG rather than anything from
// the platform, so a test can pin one exact landscape -- a crash that only
// happens on one hillside is worth being able to replay.
class GravityTerrain {
 public:
  static constexpr int16_t kWorldWidth = 360;
  static constexpr int kPoints = 31;
  static constexpr int16_t kSegmentWidth = kWorldWidth / (kPoints - 1);  // 12
  static constexpr int kMaxPads = 3;
  // The ground lives in this band: high enough that the deepest valley is
  // still inside the round bezel, low enough to leave sky to fall through.
  static constexpr int16_t kHighestGroundY = 228;
  static constexpr int16_t kLowestGroundY = 300;
  // How much the ground may rise or fall between two adjacent points.
  // Unbounded, random heights 12 px apart make a sawtooth rather than a
  // landscape: nothing to fly between, and a filled hillside would step
  // visibly away from the ridge line drawn over it. Slopes are gentle
  // enough now that the fill hides under the line.
  static constexpr int16_t kMaxSlopePerSegment = 18;

  // How far either side of the screen's centre is still visible at this
  // height. The caller supplies it so this stays free of display headers,
  // and so a test can hand it a circle, a rectangle, or no limit at all.
  using HalfWidthFn = int32_t (*)(int32_t y);

  // Pad widths in whole segments, widest first, with what each pays. The
  // narrowest is still wider than the craft; a pad you cannot fit on is
  // not a challenge, it is a joke.
  static constexpr int kPadSegments[kMaxPads] = {4, 3, 2};
  static constexpr uint8_t kPadMultipliers[kMaxPads] = {2, 3, 5};

  void generate(uint32_t seed, HalfWidthFn halfWidthAt) {
    state_ = seed ? seed : 1u;
    padCount_ = 0;

    for (int i = 0; i < kPoints; ++i) {
      points_[i].x = static_cast<int16_t>(i * kSegmentWidth);
      points_[i].y = static_cast<int16_t>(
          kHighestGroundY + next(kLowestGroundY - kHighestGroundY + 1));
    }
    // The world wraps, so the two ends are the same place. Smoothed in
    // both directions, so no pair of neighbours -- the seam included --
    // is steeper than kMaxSlopePerSegment.
    smooth();
    points_[kPoints - 1].y = points_[0].y;
    smoothBackwards();

    for (int pad = 0; pad < kMaxPads; ++pad) {
      placePad(kPadSegments[pad], kPadMultipliers[pad], halfWidthAt);
    }
  }

  // Ground height at x, interpolated between the two nearest points. x
  // wraps, like the world does.
  int16_t heightAt(int32_t x) const {
    int32_t wrapped = x % kWorldWidth;
    if (wrapped < 0) wrapped += kWorldWidth;
    const int index = static_cast<int>(wrapped / kSegmentWidth);
    const int32_t into = wrapped - index * kSegmentWidth;
    const int16_t left = points_[index].y;
    const int16_t right = points_[index + 1 < kPoints ? index + 1 : 0].y;
    return static_cast<int16_t>(left + (right - left) * into / kSegmentWidth);
  }

  // The pad under x, or nullptr where there is only hillside.
  const Pad *padAt(int32_t x) const {
    int32_t wrapped = x % kWorldWidth;
    if (wrapped < 0) wrapped += kWorldWidth;
    for (int i = 0; i < padCount_; ++i) {
      if (wrapped >= pads_[i].left && wrapped <= pads_[i].right) return &pads_[i];
    }
    return nullptr;
  }

  const TerrainPoint *profile() const { return points_; }
  static constexpr int pointCount() { return kPoints; }
  const Pad *pads() const { return pads_; }
  int padCount() const { return padCount_; }

 private:
  // Numerical Recipes' LCG. Small, deterministic, and nobody's dice rolls
  // depend on it being a good one.
  uint32_t next() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_ >> 16;
  }
  uint32_t next(uint32_t bound) { return bound ? next() % bound : 0; }

  void smooth() {
    for (int i = 1; i < kPoints; ++i) {
      const int16_t delta =
          static_cast<int16_t>(points_[i].y - points_[i - 1].y);
      if (delta > kMaxSlopePerSegment) {
        points_[i].y = static_cast<int16_t>(points_[i - 1].y + kMaxSlopePerSegment);
      } else if (delta < -kMaxSlopePerSegment) {
        points_[i].y = static_cast<int16_t>(points_[i - 1].y - kMaxSlopePerSegment);
      }
    }
  }

  void smoothBackwards() {
    for (int i = kPoints - 2; i >= 0; --i) {
      const int16_t delta =
          static_cast<int16_t>(points_[i].y - points_[i + 1].y);
      if (delta > kMaxSlopePerSegment) {
        points_[i].y = static_cast<int16_t>(points_[i + 1].y + kMaxSlopePerSegment);
      } else if (delta < -kMaxSlopePerSegment) {
        points_[i].y = static_cast<int16_t>(points_[i + 1].y - kMaxSlopePerSegment);
      }
    }
  }

  bool overlapsExistingPad(int firstSegment, int segments) const {
    const int16_t left = static_cast<int16_t>(firstSegment * kSegmentWidth);
    const int16_t right =
        static_cast<int16_t>((firstSegment + segments) * kSegmentWidth);
    for (int i = 0; i < padCount_; ++i) {
      // A segment of clearance either side, so two pads never read as one.
      if (right + kSegmentWidth >= pads_[i].left &&
          left - kSegmentWidth <= pads_[i].right) {
        return true;
      }
    }
    return false;
  }

  // Flattens `segments` segments somewhere that is free, and visible.
  // Gives up after a bounded number of tries rather than looping: a
  // landscape with two pads is still playable, a hang is not.
  void placePad(int segments, uint8_t multiplier, HalfWidthFn halfWidthAt) {
    constexpr int kTries = 40;
    for (int attempt = 0; attempt < kTries; ++attempt) {
      const int firstSegment =
          static_cast<int>(next(kPoints - 1 - segments));
      if (overlapsExistingPad(firstSegment, segments)) continue;

      const int16_t left = static_cast<int16_t>(firstSegment * kSegmentWidth);
      const int16_t right =
          static_cast<int16_t>((firstSegment + segments) * kSegmentWidth);
      const int16_t y = points_[firstSegment].y;

      // Both ends -- and so the whole pad and the multiplier drawn above
      // it -- have to be somewhere the round bezel actually shows.
      const int32_t halfWidth = halfWidthAt(y);
      const int32_t centre = kWorldWidth / 2;
      if (left < centre - halfWidth || right > centre + halfWidth) continue;

      for (int i = firstSegment; i <= firstSegment + segments; ++i) {
        points_[i].y = y;
      }
      pads_[padCount_++] = Pad{left, right, y, multiplier};
      return;
    }
  }

  TerrainPoint points_[kPoints] = {};
  Pad pads_[kMaxPads] = {};
  int padCount_ = 0;
  uint32_t state_ = 1;
};

}  // namespace knobify::games
