#pragma once

#include <driver/i2c.h>

#include "TouchDriver.h"

namespace knobify::drivers {

// Touch pins/address, per device.md's pinout. Shares the I2C bus with
// the (out-of-v1-scope) DRV2605 haptics driver.
constexpr int kTouchSdaPin = 11;
constexpr int kTouchSclPin = 12;
constexpr uint8_t kTouchI2cAddress = 0x15;
constexpr i2c_port_t kTouchI2cPort = I2C_NUM_0;

// CST816 touch controller, polled over I2C -- protocol ported from
// Waveshare's own official demo for this board (register 0x00, 7 bytes:
// byte[2] = touch point count, bytes[3-4] = 12-bit X, bytes[5-6] = 12-bit
// Y). This chip isn't interrupt-driven here (the demo doesn't use the
// INT pin either); poll() always reports the current state, and lets
// GestureRecognizer infer down/up transitions from the pressed sequence.
class Cst816Driver : public input::TouchDriver {
 public:
  bool begin() {
    const i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = kTouchSdaPin,
        .scl_io_num = kTouchSclPin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {.clk_speed = 300 * 1000},
        .clk_flags = 0,
    };
    if (i2c_param_config(kTouchI2cPort, &config) != ESP_OK) return false;
    if (i2c_driver_install(kTouchI2cPort, config.mode, 0, 0, 0) != ESP_OK) {
      return false;
    }
    uint8_t normalMode = 0x00;
    writeRegister(0x00, &normalMode, 1);
    return true;
  }

  bool poll(input::TouchSample &out) override {
    uint8_t data[7] = {0};
    readRegister(0x00, data, sizeof(data));
    bool touched = data[2] != 0;
    if (touched) {
      lastX_ = static_cast<int16_t>(((data[3] & 0x0F) << 8) | data[4]);
      lastY_ = static_cast<int16_t>(((data[5] & 0x0F) << 8) | data[6]);
    }
    out = input::TouchSample{lastX_, lastY_, touched};
    return true;
  }

  // Before the sleep timer's deep sleep (ADR 0015): the chip must keep
  // scanning and pull INT low on a touch, since that is what wakes the
  // ESP32. Register values from the CST816S datasheet (0xFA IrqCtl:
  // EnTouch | EnChange; 0xFE DisAutoSleep) -- unverified on this board.
  void armWakeOnTouch() {
    uint8_t irqOnTouch = 0x60;
    writeRegister(0xFA, &irqOnTouch, 1);
    uint8_t noAutoSleep = 0x01;
    writeRegister(0xFE, &noAutoSleep, 1);
  }

 private:
  void writeRegister(uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[8];
    buf[0] = reg;
    for (size_t i = 0; i < len; ++i) buf[i + 1] = data[i];
    i2c_master_write_to_device(kTouchI2cPort, kTouchI2cAddress, buf, len + 1,
                                1000 / portTICK_PERIOD_MS);
  }

  void readRegister(uint8_t reg, uint8_t *out, size_t len) {
    i2c_master_write_read_device(kTouchI2cPort, kTouchI2cAddress, &reg, 1,
                                  out, len, 1000 / portTICK_PERIOD_MS);
  }

  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
};

}  // namespace knobify::drivers
