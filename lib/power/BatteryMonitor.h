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
// No charging detection: a 2026-09-13 hardware probe (see the
// battery-indicator plan doc) found no dedicated charge-status pin, no
// voltage jump on USB plug/unplug, and no status LED on the board --
// GPIO1 only ever reflects battery voltage, not charge state. Left out
// of this class entirely rather than faked with an unreliable
// heuristic.
//
// Calibration caveat: kEmptyMilliVolts/kFullMilliVolts below are
// placeholders based on a single observed reading (~2400mV, battery
// attached and charging) -- the true battery-voltage-to-ADC-mV
// relationship (divider ratio, if any) hasn't been cross-checked
// against a multimeter yet. Recalibrate these once that's done.
class BatteryMonitor {
 public:
  static constexpr uint32_t kEmptyMilliVolts = 1800;
  static constexpr uint32_t kFullMilliVolts = 2450;

  enum class Level { kEmpty, kLow, kMedium, kHigh, kFull };

  void update(uint32_t milliVolts) { percent_ = toPercent(milliVolts); }

  int percent() const { return percent_; }

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
};

}  // namespace knobify::power
