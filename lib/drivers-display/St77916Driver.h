#pragma once

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#include "Sh8601InitCmds.h"
#include "esp_lcd_sh8601.h"

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
constexpr int kLcdBitsPerPixel = 16;

// QSPI init/bring-up for the ST77916 panel, ported from Waveshare's own
// official demo for this board (see Sh8601InitCmds.h) -- the panel
// shares its command set with the vendored SH8601 driver
// (esp_lcd_sh8601.c/.h). Owns the SPI bus, the panel handle, and the
// backlight PWM channel; LVGL glue (disp_drv registration, flush
// callback) lives separately in lib/ui/ since it also needs the touch
// driver.
class St77916Driver {
 public:
  // `onColorTransDone`/`callbackContext` are supplied by the LVGL glue
  // layer (lib/ui/) so it can be notified when a flush finishes -- this
  // driver doesn't know about LVGL's lv_disp_drv_t itself.
  bool begin(esp_lcd_panel_io_color_trans_done_cb_t onColorTransDone,
             void *callbackContext) {
    const spi_bus_config_t busConfig = SH8601_PANEL_BUS_QSPI_CONFIG(
        kLcdClkPin, kLcdD0Pin, kLcdD1Pin, kLcdD2Pin, kLcdD3Pin,
        kLcdHorRes * kLcdVerRes * kLcdBitsPerPixel / 8);
    if (spi_bus_initialize(kSpiHost, &busConfig, SPI_DMA_CH_AUTO) != ESP_OK) {
      return false;
    }

    const esp_lcd_panel_io_spi_config_t ioConfig = SH8601_PANEL_IO_QSPI_CONFIG(
        kLcdCsPin, onColorTransDone, callbackContext);
    if (esp_lcd_new_panel_io_spi(
            reinterpret_cast<esp_lcd_spi_bus_handle_t>(kSpiHost), &ioConfig,
            &ioHandle_) != ESP_OK) {
      return false;
    }

    sh8601_vendor_config_t vendorConfig = {
        .init_cmds = kSt77916InitCmds,
        .init_cmds_size = kSt77916InitCmdsCount,
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panelConfig = {
        .reset_gpio_num = kLcdRstPin,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = kLcdBitsPerPixel,
        .vendor_config = &vendorConfig,
    };
    if (esp_lcd_new_panel_sh8601(ioHandle_, &panelConfig, &panelHandle_) !=
        ESP_OK) {
      return false;
    }
    if (esp_lcd_panel_reset(panelHandle_) != ESP_OK) return false;
    if (esp_lcd_panel_init(panelHandle_) != ESP_OK) return false;

    initBacklight();
    return true;
  }

  esp_lcd_panel_handle_t panelHandle() const { return panelHandle_; }

  // 0-255.
  void setBacklight(uint8_t duty) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
  }

 private:
  static constexpr spi_host_device_t kSpiHost = SPI2_HOST;
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

  esp_lcd_panel_io_handle_t ioHandle_ = nullptr;
  esp_lcd_panel_handle_t panelHandle_ = nullptr;
};

}  // namespace knobify::drivers
