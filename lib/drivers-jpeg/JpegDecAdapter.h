#pragma once

#include <Arduino.h>
#include <JPEGDEC.h>

#include <memory>
#include <vector>

#include "CoverArtCache.h"
#include "SquareResampler.h"

namespace knobify::drivers {

// Decodes a JPEG (from memory) into a square RGB565 cover with JPEGDEC.
// Baseline images use the largest of JPEGDEC's 1/2, 1/4, 1/8 scales that
// still covers outSize. Progressive images -- ~90% of this library's
// embedded art -- are decoded from their DC coefficients only, which is
// exactly an 8x8 block average: 19 ms and ~11 KB for a 600 px cover,
// 234 ms for 3000 px on the device (ADR 0016). A full progressive decode
// (stb_image) needed 4.2 MB of PSRAM at 600 px and ran out above ~1000 px.
// SquareResampler then crops and scales to outSize.
class JpegDecAdapter : public library::JpegDecoder {
 public:
  bool decodeSquare(const uint8_t *jpegBytes, size_t jpegLen,
                     uint16_t outSize,
                     std::vector<uint16_t> *outPixels) override {
    // Several KB of working buffers: allocated per decode (in PSRAM, see
    // heap_caps_malloc_extmem_enable() in setup()) rather than kept.
    auto decoder = std::make_unique<JPEGDEC>();
    // openRAM() takes a non-const pointer but only reads.
    if (!decoder->openRAM(const_cast<uint8_t *>(jpegBytes),
                          static_cast<int>(jpegLen), &JpegDecAdapter::draw)) {
      Serial.printf("[cover] JPEGDEC open failed, jpegLen=%u error=%d\n",
                    static_cast<unsigned>(jpegLen), decoder->getLastError());
      return false;
    }
    const int width = decoder->getWidth();
    const int height = decoder->getHeight();
    int scale = 1;
    if (decoder->getJPEGType() == JPEG_MODE_PROGRESSIVE) {
      scale = 8;
    } else {
      for (int candidate : {2, 4, 8}) {
        if (width / candidate >= outSize && height / candidate >= outSize) {
          scale = candidate;
        }
      }
    }
    scratchWidth_ = static_cast<uint16_t>((width + scale - 1) / scale);
    scratchHeight_ = static_cast<uint16_t>((height + scale - 1) / scale);
    scratch_.assign(static_cast<size_t>(scratchWidth_) * scratchHeight_, 0);

    decoder->setUserPointer(this);
    const int options = scale == 8   ? JPEG_SCALE_EIGHTH
                        : scale == 4 ? JPEG_SCALE_QUARTER
                        : scale == 2 ? JPEG_SCALE_HALF
                                     : 0;
    const bool ok = decoder->decode(0, 0, options) == 1;
    const int error = decoder->getLastError();
    decoder->close();
    if (!ok) {
      Serial.printf("[cover] JPEGDEC decode failed, %dx%d error=%d\n", width,
                    height, error);
      return false;
    }
    library::SquareResampler::resample(scratch_.data(), scratchWidth_,
                                       scratchHeight_, outSize, outPixels);
    scratch_.clear();
    scratch_.shrink_to_fit();
    return true;
  }

 private:
  // Copies one decoded block into scratch_; JPEGDEC hands blocks over in
  // RGB565, native byte order, matching what CoverWriter stores.
  static int draw(JPEGDRAW *block) {
    auto *self = static_cast<JpegDecAdapter *>(block->pUser);
    for (int row = 0; row < block->iHeight; ++row) {
      const int y = block->y + row;
      if (y >= self->scratchHeight_) break;
      for (int col = 0; col < block->iWidth; ++col) {
        const int x = block->x + col;
        if (x >= self->scratchWidth_) break;
        self->scratch_[static_cast<size_t>(y) * self->scratchWidth_ + x] =
            block->pPixels[row * block->iWidth + col];
      }
    }
    return 1;
  }

  std::vector<uint16_t> scratch_;
  uint16_t scratchWidth_ = 0;
  uint16_t scratchHeight_ = 0;
};

}  // namespace knobify::drivers
