#pragma once

#include <SD_MMC.h>

#include <cstring>
#include <string>
#include <vector>

#include "CoverArtCache.h"

namespace knobify::drivers {

// Cached-cover file format: a small fixed header, then a raw RGB565
// pixel dump -- decoding is done once at scan time (CoverArtCache), so
// render time (ScreenManager) only ever needs a plain file read, no
// JPEG decoding. See SdCoverReader for the matching read side.
struct CoverFileHeader {
  char magic[4];  // "CVR1"
  uint16_t width;
  uint16_t height;
};

// Where cached covers live on the SD card, one file per album folder --
// see lib/library/CoverArtCache.h for the folder-path -> filename
// mapping (shared with SdCoverReader so both sides agree).
constexpr const char *kCoverCacheDir = "/knobify/covers";

inline std::string coverCachePathFor(const std::string &albumFolderPath) {
  return std::string(kCoverCacheDir) + "/" +
         library::CoverArtCache::cacheFileNameFor(albumFolderPath) + ".rgb";
}

class SdCoverWriter : public library::CoverWriter {
 public:
  void writeCover(const std::string &albumFolderPath, uint16_t size,
                   const std::vector<uint16_t> &pixels) override {
    // Mirrors writeIndexCacheFile()'s mkdir-before-open in main.cpp --
    // SD_MMC/FATFS refuses to create a file inside a directory that
    // doesn't exist yet.
    SD_MMC.mkdir(kCoverCacheDir);
    std::string path = coverCachePathFor(albumFolderPath);
    fs::File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!file) {
      Serial.printf("[cover] could not open %s for write\n", path.c_str());
      return;
    }
    CoverFileHeader header;
    std::memcpy(header.magic, "CVR1", 4);
    header.width = size;
    header.height = size;
    file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header));
    file.write(reinterpret_cast<const uint8_t *>(pixels.data()),
               pixels.size() * sizeof(uint16_t));
    file.close();
  }
};

}  // namespace knobify::drivers
