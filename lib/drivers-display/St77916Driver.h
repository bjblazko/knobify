#pragma once

#include <Arduino_GFX_Library.h>
#include <driver/ledc.h>

#include "KnobSt77916.h"

namespace knobify::drivers {

// Display pins, per device.md's pinout.
constexpr int kLcdClkPin = 13;
constexpr int kLcdD0Pin = 15;
constexpr int kLcdD1Pin = 16;
constexpr int kLcdD2Pin = 17;
constexpr int kLcdD3Pin = 18;
constexpr int kLcdCsPin = 14;
constexpr int kLcdRstPin = 21;
constexpr int kLcdBacklightPin = 47;

constexpr int kLcdHorRes = 360;
constexpr int kLcdVerRes = 360;

// QSPI bring-up for the ST77916 panel via Arduino_GFX
// (Arduino_ESP32QSPI + Arduino_ST77916), using this project's own
// verified init sequence (St77916InitOps.h) rather than Arduino_GFX's
// built-in one (different panel calibration values). Arduino_GFX
// implements QSPI transactions itself directly against spi_master,
// unlike ESP-IDF's esp_lcd_panel_io_spi -- see platformio.ini for why
// that matters on this project's bundled ESP-IDF version.
class St77916Driver {
 public:
  bool begin() {
    bus_ = new Arduino_ESP32QSPI(kLcdCsPin, kLcdClkPin, kLcdD0Pin, kLcdD1Pin,
                                  kLcdD2Pin, kLcdD3Pin);
    gfx_ = new KnobSt77916(bus_, kLcdRstPin, /*rotation=*/0, /*ips=*/true,
                            kLcdHorRes, kLcdVerRes);
    if (!gfx_->begin()) {
      return false;
    }
    initBacklight();
    return true;
  }

  Arduino_TFT *gfx() const { return gfx_; }

  // 0-255.
  void setBacklight(uint8_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
  }

 private:
  static constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_1;
  static constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_3;

  void initBacklight() {
    const ledc_timer_config_t timerConfig = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = kBacklightTimer,
        .freq_hz = 50 * 1000,
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    ledc_timer_config(&timerConfig);
    const ledc_channel_config_t channelConfig = {
        .gpio_num = kLcdBacklightPin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = kBacklightChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kBacklightTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channelConfig);
    setBacklight(255);
  }

  Arduino_DataBus *bus_ = nullptr;
  Arduino_TFT *gfx_ = nullptr;
};

}  // namespace knobify::drivers
