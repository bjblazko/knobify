#pragma once

#include <Arduino.h>
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
  // Belt-and-suspenders: the SD spec requires pull-ups on CMD/D0-D3, and
  // this board's reference design apparently doesn't populate them
  // externally, unlike most. Doesn't hurt, though it turned out NOT to be
  // the fix for the "almost every file fails to read" issue -- that was
  // actually the SD card's filesystem layout (macOS Disk Utility's FAT32
  // vs. the SD Association's own format spec). See AGENTS.md and ADR 0004's
  // Implementation status.
  pinMode(kSdCmdPin, INPUT_PULLUP);
  pinMode(kSdD0Pin, INPUT_PULLUP);
  pinMode(kSdD1Pin, INPUT_PULLUP);
  pinMode(kSdD2Pin, INPUT_PULLUP);
  pinMode(kSdD3Pin, INPUT_PULLUP);

  SD_MMC.setPins(kSdClkPin, kSdCmdPin, kSdD0Pin, kSdD1Pin, kSdD2Pin,
                  kSdD3Pin);
  return SD_MMC.begin("/sdcard", /*mode1bit=*/false,
                       /*format_if_mount_failed=*/false,
                       SDMMC_FREQ_HIGHSPEED);
}

}  // namespace knobify::drivers
