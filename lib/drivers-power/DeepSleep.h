#pragma once

#include <driver/rtc_io.h>
#include <esp_sleep.h>

namespace knobify::drivers {

// The CST816's interrupt output, per device.md's pinout: pulled low while
// a finger is on the screen. An RTC-capable pin on the ESP32-S3.
constexpr gpio_num_t kTouchIntPin = GPIO_NUM_9;

// Deep sleep for the sleep timer (ADR 0015). The caller has already
// saved everything worth keeping and turned the display and audio off;
// this never returns. A touch wakes the chip, which then boots normally
// (esp_reset_reason() == ESP_RST_DEEPSLEEP) and resumes (ADR 0012).
// Only the touch wakes it, not the knob: a nudge in a pocket must not
// reboot the device.
[[noreturn]] inline void enterDeepSleepUntilTouch() {
  rtc_gpio_pullup_en(kTouchIntPin);
  rtc_gpio_pulldown_dis(kTouchIntPin);
  esp_sleep_enable_ext0_wakeup(kTouchIntPin, 0);
  esp_deep_sleep_start();
}

}  // namespace knobify::drivers
