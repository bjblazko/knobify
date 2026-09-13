#pragma once

#include <Arduino.h>

namespace knobify::drivers {

// device.md pinout: "Other | Battery ADC | 1". Confirmed by a live
// serial probe (2026-09-13, see the battery-indicator plan doc) to
// return a stable, plausible reading (~2400mV with the battery attached
// and charging) rather than floating noise -- but the exact
// battery-voltage-to-ADC-mV relationship (divider ratio, if any) is not
// yet cross-checked against a multimeter. See power::BatteryMonitor for
// where that calibration caveat is tracked.
constexpr int kBatteryAdcPin = 1;

// GPIO1 sits behind a 2:1 divider: the pin reads ~2400mV while the rail
// is ~4.8V on USB. readMilliVolts() returns the undivided battery-rail
// voltage, the same scale the factory firmware logs ("Battery : %u mv").
constexpr uint32_t kBatteryDividerRatio = 2;

// Thin wrapper around the ESP32 Arduino core's calibrated ADC read --
// no logic of its own. power::BatteryMonitor (host-testable) turns the
// millivolt reading this returns into a percent/level estimate.
class BatteryAdcDriver {
 public:
  void begin() { analogReadResolution(12); }

  uint32_t readMilliVolts() const {
    return analogReadMilliVolts(kBatteryAdcPin) * kBatteryDividerRatio;
  }
};

}  // namespace knobify::drivers
