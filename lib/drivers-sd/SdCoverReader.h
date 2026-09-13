#pragma once

#include <SD_MMC.h>

#include <cstring>
#include <string>
#include <vector>

#include "CoverArtCache.h"
#include "SdCoverWriter.h"  // CoverFileHeader, coverCachePathFor()

namespace knobify::drivers {

// Reads back a cover cached by SdCoverWriter -- used by the Now Playing
// screen at render time. Plain file read, no JPEG decoding involved
// (decoding happened once, at scan time).
class SdCoverReader : public library::CoverArtReader {
 public:
  bool loadCover(const std::string &albumFolderPath, uint16_t *outSize,
                 std::vector<uint16_t> *outPixels) override {
    std::string path = coverCachePathFor(albumFolderPath);
    fs::File file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file) {
      return false;  // No cover cached yet -- not an error.
    }
    CoverFileHeader header;
    if (file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) !=
            sizeof(header) ||
        std::memcmp(header.magic, "CVR1", 4) != 0 ||
        header.width == 0 || header.width != header.height) {
      file.close();
      return false;
    }
    size_t pixelCount = static_cast<size_t>(header.width) * header.height;
    outPixels->resize(pixelCount);
    size_t bytesToRead = pixelCount * sizeof(uint16_t);
    bool ok = file.read(reinterpret_cast<uint8_t *>(outPixels->data()),
                         bytesToRead) == bytesToRead;
    file.close();
    if (!ok) {
      return false;
    }
    *outSize = header.width;
    return true;
  }
};

}  // namespace knobify::drivers
