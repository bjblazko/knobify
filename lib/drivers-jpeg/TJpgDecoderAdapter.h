#pragma once

#include <TJpg_Decoder.h>

#include <algorithm>
#include <vector>

#include "CoverArtCache.h"

namespace knobify::drivers {

// Decodes a JPEG (from memory) into a fixed-size square RGB565 buffer,
// for CoverArtCache to write to SD at scan time. TJpg_Decoder only
// supports integer downscale factors (1/2/4/8) and delivers pixels
// through a global callback, so this: (1) picks the smallest supported
// scale that still leaves the decoded image at least outSize on both
// axes, (2) decodes into a scratch buffer at that scaled resolution, (3)
// center-crops to a square and nearest-neighbor-resamples down to
// exactly outSize x outSize. Runs only at scan time (never during
// playback) -- see docs for the "cache pre-decoded RGB565" design.
class TJpgDecoderAdapter : public library::JpegDecoder {
 public:
  bool decodeSquare(const uint8_t *jpegBytes, size_t jpegLen,
                     uint16_t outSize,
                     std::vector<uint16_t> *outPixels) override {
    uint16_t width = 0;
    uint16_t height = 0;
    if (TJpgDec.getJpgSize(&width, &height, jpegBytes, jpegLen) != 0 ||
        width == 0 || height == 0) {
      // Most commonly a Progressive JPEG -- TJpg_Decoder only supports
      // baseline. See AGENTS.md; convert-music-library.sh re-encodes
      // embedded art to baseline for exactly this reason.
      Serial.printf("[cover] getJpgSize failed (progressive JPEG?), "
                    "jpegLen=%u\n",
                    static_cast<unsigned>(jpegLen));
      return false;
    }

    uint8_t scale = 1;
    for (uint8_t candidate : {1, 2, 4, 8}) {
      if (width / candidate >= outSize && height / candidate >= outSize) {
        scale = candidate;
      }
    }
    TJpgDec.setJpgScale(scale);

    scratchWidth_ = width / scale;
    scratchHeight_ = height / scale;
    scratch_.assign(static_cast<size_t>(scratchWidth_) * scratchHeight_, 0);

    TJpgDec.setCallback(&TJpgDecoderAdapter::tileCallback);
    s_active = this;
    bool decodeOk = TJpgDec.drawJpg(0, 0, jpegBytes, jpegLen) == 0;
    s_active = nullptr;
    if (!decodeOk) {
      Serial.printf("[cover] drawJpg failed, jpegLen=%u\n",
                    static_cast<unsigned>(jpegLen));
      return false;
    }

    uint16_t cropSize = std::min(scratchWidth_, scratchHeight_);
    uint16_t cropX = (scratchWidth_ - cropSize) / 2;
    uint16_t cropY = (scratchHeight_ - cropSize) / 2;

    outPixels->resize(static_cast<size_t>(outSize) * outSize);
    for (uint16_t y = 0; y < outSize; ++y) {
      uint16_t srcY = cropY + static_cast<uint16_t>(
                                  (static_cast<uint32_t>(y) * cropSize) / outSize);
      for (uint16_t x = 0; x < outSize; ++x) {
        uint16_t srcX =
            cropX + static_cast<uint16_t>(
                        (static_cast<uint32_t>(x) * cropSize) / outSize);
        (*outPixels)[static_cast<size_t>(y) * outSize + x] =
            scratch_[static_cast<size_t>(srcY) * scratchWidth_ + srcX];
      }
    }
    return true;
  }

 private:
  // TJpg_Decoder's callback API is a single global slot, not tied to any
  // particular decoder instance -- s_active points at whichever
  // TJpgDecoderAdapter is currently mid-decode (decoding only ever
  // happens one album at a time, at scan time, never concurrently).
  static bool tileCallback(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t *bitmap) {
    if (!s_active) return false;
    return s_active->writeTile(x, y, w, h, bitmap);
  }

  bool writeTile(int16_t x, int16_t y, uint16_t w, uint16_t h,
                 uint16_t *bitmap) {
    for (uint16_t row = 0; row < h; ++row) {
      int32_t destY = y + row;
      if (destY < 0 || destY >= scratchHeight_) continue;
      for (uint16_t col = 0; col < w; ++col) {
        int32_t destX = x + col;
        if (destX < 0 || destX >= scratchWidth_) continue;
        scratch_[static_cast<size_t>(destY) * scratchWidth_ + destX] =
            bitmap[static_cast<size_t>(row) * w + col];
      }
    }
    return true;
  }

  std::vector<uint16_t> scratch_;
  uint16_t scratchWidth_ = 0;
  uint16_t scratchHeight_ = 0;

  static inline TJpgDecoderAdapter *s_active = nullptr;
};

}  // namespace knobify::drivers
