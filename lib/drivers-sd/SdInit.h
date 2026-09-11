#pragma once

#include <SD_MMC.h>

namespace knobify::drivers {

// SDMMC 4-wire pins per device.md's pinout (sourced from a community
// reference for this exact board, not yet independently verified against
// the physical hardware -- confirm on first flash).
constexpr int kSdCmdPin = 3;
constexpr int kSdClkPin = 4;
constexpr int kSdD0Pin = 5;
constexpr int kSdD1Pin = 6;
constexpr int kSdD2Pin = 42;
constexpr int kSdD3Pin = 2;

inline bool initSdCard() {
  SD_MMC.setPins(kSdClkPin, kSdCmdPin, kSdD0Pin, kSdD1Pin, kSdD2Pin,
                  kSdD3Pin);
  return SD_MMC.begin("/sdcard", /*mode1bit=*/false);
}

}  // namespace knobify::drivers
