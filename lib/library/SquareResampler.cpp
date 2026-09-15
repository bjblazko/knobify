#include "SquareResampler.h"

#include <algorithm>

namespace knobify::library {

namespace {

struct Rgb {
  uint32_t r = 0;
  uint32_t g = 0;
  uint32_t b = 0;
};

Rgb channels(uint16_t p) { return {static_cast<uint32_t>(p >> 11), (p >> 5) & 0x3Fu, p & 0x1Fu}; }

uint16_t pack(uint32_t r, uint32_t g, uint32_t b) {
  return static_cast<uint16_t>((std::min(r, 31u) << 11) | (std::min(g, 63u) << 5) |
                               std::min(b, 31u));
}

}  // namespace

void SquareResampler::resample(const uint16_t *src, uint16_t width,
                               uint16_t height, uint16_t outSize,
                               std::vector<uint16_t> *out) {
  out->assign(static_cast<size_t>(outSize) * outSize, 0);
  if (width == 0 || height == 0 || outSize == 0) return;
  const uint32_t crop = std::min(width, height);
  const uint32_t cropX = (width - crop) / 2;
  const uint32_t cropY = (height - crop) / 2;
  auto at = [&](uint32_t x, uint32_t y) {
    return src[(cropY + y) * static_cast<size_t>(width) + cropX + x];
  };

  for (uint32_t oy = 0; oy < outSize; ++oy) {
    for (uint32_t ox = 0; ox < outSize; ++ox) {
      Rgb sum;
      if (crop >= outSize) {
        // Average every source pixel whose box maps onto this output pixel.
        uint32_t x0 = ox * crop / outSize;
        uint32_t x1 = std::max(x0 + 1, (ox + 1) * crop / outSize);
        uint32_t y0 = oy * crop / outSize;
        uint32_t y1 = std::max(y0 + 1, (oy + 1) * crop / outSize);
        for (uint32_t y = y0; y < y1; ++y) {
          for (uint32_t x = x0; x < x1; ++x) {
            Rgb c = channels(at(x, y));
            sum.r += c.r;
            sum.g += c.g;
            sum.b += c.b;
          }
        }
        uint32_t n = (x1 - x0) * (y1 - y0);
        (*out)[oy * outSize + ox] = pack((sum.r + n / 2) / n, (sum.g + n / 2) / n,
                                         (sum.b + n / 2) / n);
      } else {
        // Bilinear, sampling at pixel centers; weights in 1/256.
        int32_t fx = static_cast<int32_t>(((2 * ox + 1) * crop * 256) / (2 * outSize)) - 128;
        int32_t fy = static_cast<int32_t>(((2 * oy + 1) * crop * 256) / (2 * outSize)) - 128;
        fx = std::max(fx, 0);
        fy = std::max(fy, 0);
        uint32_t x0 = std::min<uint32_t>(fx >> 8, crop - 1);
        uint32_t y0 = std::min<uint32_t>(fy >> 8, crop - 1);
        uint32_t x1 = std::min(x0 + 1, crop - 1);
        uint32_t y1 = std::min(y0 + 1, crop - 1);
        uint32_t wx = fx & 0xFF;
        uint32_t wy = fy & 0xFF;
        Rgb a = channels(at(x0, y0)), b = channels(at(x1, y0));
        Rgb c = channels(at(x0, y1)), d = channels(at(x1, y1));
        auto mix = [&](uint32_t pa, uint32_t pb, uint32_t pc, uint32_t pd) {
          uint32_t top = pa * (256 - wx) + pb * wx;
          uint32_t bottom = pc * (256 - wx) + pd * wx;
          return (top * (256 - wy) + bottom * wy + 32768) >> 16;
        };
        (*out)[oy * outSize + ox] =
            pack(mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b));
      }
    }
  }
}

}  // namespace knobify::library
