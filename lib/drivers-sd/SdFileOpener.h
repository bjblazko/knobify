#pragma once

#include <SD_MMC.h>

#include <memory>

#include "LibraryScanner.h"
#include "SdRawFile.h"

namespace knobify::drivers {

// Opens files on the SD card for tag reading during a library scan.
// Uses SD_MMC (4-wire SDMMC, per device.md's pinout) rather than the SPI
// `SD` library, since that's how this board's SD socket is wired.
class SdFileOpener : public library::FileOpener {
 public:
  std::unique_ptr<library::RawFile> open(const std::string &path) override {
    fs::File file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
      return nullptr;
    }
    return std::make_unique<SdRawFile>(std::move(file));
  }
};

}  // namespace knobify::drivers
