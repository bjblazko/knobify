#pragma once

#include <cstdint>
#include <vector>

namespace knobify::library {

// Center-crops an RGB565 image to a square and scales it to outSize x
// outSize: an area average when shrinking (no aliasing on detailed covers),
// bilinear when enlarging (a progressive JPEG decoded at 1/8 scale can come
// out smaller than the cover slot, e.g. 600 px -> 75 px for a 96 px slot).
class SquareResampler {
 public:
  static void resample(const uint16_t *src, uint16_t width, uint16_t height,
                       uint16_t outSize, std::vector<uint16_t> *out);
};

}  // namespace knobify::library
