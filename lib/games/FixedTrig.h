#pragma once

#include <cstdint>

namespace knobify::games {

// Integer sine and cosine for the games (ADR 0023), in degrees, scaled to
// kScale. No floating point: the same call has to produce the craft's
// thrust direction and the outline drawn for it, and a test has to be
// able to replay a whole flight and get the same numbers every time.
// A quarter turn is tabulated and the other three are mirrored from it.
class FixedTrig {
 public:
  static constexpr int32_t kScale = 1024;

  // angleDeg may be any value, positive or negative.
  static constexpr int32_t sinScaled(int32_t angleDeg) {
    int32_t a = angleDeg % 360;
    if (a < 0) a += 360;
    if (a <= 90) return kQuarter[a];
    if (a <= 180) return kQuarter[180 - a];
    if (a <= 270) return -kQuarter[a - 180];
    return -kQuarter[360 - a];
  }

  static constexpr int32_t cosScaled(int32_t angleDeg) {
    return sinScaled(angleDeg + 90);
  }

 private:
  // sin(0..90 degrees) * kScale, rounded.
  static constexpr int32_t kQuarter[91] = {
      0, 18, 36, 54, 71, 89, 107, 125, 143, 160,
      178, 195, 213, 230, 248, 265, 282, 299, 316, 333,
      350, 367, 384, 400, 416, 433, 449, 465, 481, 496,
      512, 527, 543, 558, 573, 587, 602, 616, 630, 644,
      658, 672, 685, 698, 711, 724, 737, 749, 761, 773,
      784, 796, 807, 818, 828, 839, 849, 859, 868, 878,
      887, 896, 904, 912, 920, 928, 935, 943, 949, 956,
      962, 968, 974, 979, 984, 989, 994, 998, 1002, 1005,
      1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024,
      1024,
  };
};

}  // namespace knobify::games
