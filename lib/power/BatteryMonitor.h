#pragma once

#include <cstdint>

namespace knobify::power {

// Converts a Battery-ADC millivolt reading (see
// drivers::BatteryAdcDriver) into a 0-100% charge estimate and a coarse
// level bucket for UI color-coding. Pure logic, no Arduino/hardware --
// host-testable like IdleTimer/LockController; the caller (main.cpp) is
// responsible for the actual analogReadMilliVolts() call and for
// calling update() periodically (battery voltage doesn't need polling
// faster than every few seconds).
//
// Takes the battery-rail voltage (drivers::BatteryAdcDriver already
// undoes the 2:1 divider). Calibration from real-device readings
// (2026-09-13): a full cell settles around 4100mV; on USB the rail is
// lifted above 4500mV, which is the only charging signal this board
// has (no charge-status pin). While charging, the reading reflects the
// charger rather than the cell, so percent() is not meaningful then.
class BatteryMonitor {
 public:
  static constexpr uint32_t kEmptyMilliVolts = 3400;
  static constexpr uint32_t kFullMilliVolts = 4100;
  // Above this the board is on USB power and charging.
  static constexpr uint32_t kChargingAboveMilliVolts = 4500;

  enum class Level { kEmpty, kLow, kMedium, kHigh, kFull };

  void update(uint32_t milliVolts) {
    percent_ = toPercent(milliVolts);
    charging_ = milliVolts > kChargingAboveMilliVolts;
  }

  int percent() const { return percent_; }

  bool isCharging() const { return charging_; }

  Level level() const {
    if (percent_ < kLowThreshold) return Level::kEmpty;
    if (percent_ < kMediumThreshold) return Level::kLow;
    if (percent_ < kHighThreshold) return Level::kMedium;
    if (percent_ < kFullThreshold) return Level::kHigh;
    return Level::kFull;
  }

 private:
  static constexpr int kLowThreshold = 10;
  static constexpr int kMediumThreshold = 30;
  static constexpr int kHighThreshold = 55;
  static constexpr int kFullThreshold = 80;

  static int toPercent(uint32_t milliVolts) {
    if (milliVolts <= kEmptyMilliVolts) return 0;
    if (milliVolts >= kFullMilliVolts) return 100;
    return static_cast<int>((milliVolts - kEmptyMilliVolts) * 100 /
                             (kFullMilliVolts - kEmptyMilliVolts));
  }

  int percent_ = 0;
  bool charging_ = false;
};

}  // namespace knobify::power
