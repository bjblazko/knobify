#pragma once

// A minimal Arduino_GFX Arduino_TFT subclass for the ST77916, using this
// project's own verified init sequence (St77916InitOps.h) rather than
// the one built into newer Arduino_GFX releases' own Arduino_ST77916
// class. Named distinctly (not `Arduino_ST77916`) and kept out of the
// `moononournation/GFX Library for Arduino` package on purpose: that
// package is pinned to v1.4.9 here (a version old enough to not require
// esp32-hal-periman.h, which isn't present in this project's bundled
// Arduino-ESP32 core -- see platformio.ini), and v1.4.9's own
// Arduino_ST77916 doesn't accept a custom init table at all (that
// parameter was added in a later release). This class's logic mirrors
// the upstream Arduino_ST77916 (display/Arduino_ST77916.cpp in
// moononournation/Arduino_GFX) against the older, still-compatible
// Arduino_TFT/Arduino_DataBus base classes.

#include <Arduino_GFX_Library.h>

#include "St77916InitOps.h"

namespace knobify::drivers {

#define KNOB_ST77916_MADCTL_MY 0x80
#define KNOB_ST77916_MADCTL_MX 0x40
#define KNOB_ST77916_MADCTL_MV 0x20
#define KNOB_ST77916_MADCTL_RGB 0x00
#define KNOB_ST77916_MADCTL 0x36
#define KNOB_ST77916_CASET 0x2A
#define KNOB_ST77916_RASET 0x2B
#define KNOB_ST77916_RAMWR 0x2C
#define KNOB_ST77916_INVON 0x21
#define KNOB_ST77916_INVOFF 0x20
#define KNOB_ST77916_SLPOUT 0x11
#define KNOB_ST77916_SLPIN 0x10
#define KNOB_ST77916_SWRESET 0x01
#define KNOB_ST77916_RST_DELAY 120
#define KNOB_ST77916_SLPOUT_DELAY 120
#define KNOB_ST77916_SLPIN_DELAY 120

class KnobSt77916 : public Arduino_TFT {
 public:
  KnobSt77916(Arduino_DataBus *bus, int8_t rst, uint8_t r, bool ips,
              int16_t w, int16_t h)
      : Arduino_TFT(bus, rst, r, ips, w, h, 0, 0, 0, 0) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    return Arduino_TFT::begin(speed);
  }

  void setRotation(uint8_t r) override {
    Arduino_TFT::setRotation(r);
    uint8_t madctl;
    switch (_rotation) {
      case 1:
        madctl = KNOB_ST77916_MADCTL_MX | KNOB_ST77916_MADCTL_MV |
                 KNOB_ST77916_MADCTL_RGB;
        break;
      case 2:
        madctl = KNOB_ST77916_MADCTL_MX | KNOB_ST77916_MADCTL_MY |
                 KNOB_ST77916_MADCTL_RGB;
        break;
      case 3:
        madctl = KNOB_ST77916_MADCTL_MY | KNOB_ST77916_MADCTL_MV |
                 KNOB_ST77916_MADCTL_RGB;
        break;
      default:
        madctl = KNOB_ST77916_MADCTL_RGB;
        break;
    }
    _bus->beginWrite();
    _bus->writeC8D8(KNOB_ST77916_MADCTL, madctl);
    _bus->endWrite();
  }

  void writeAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) override {
    if ((x != _currentX) || (w != _currentW)) {
      _currentX = x;
      _currentW = w;
      x += _xStart;
      _bus->writeC8D16D16(KNOB_ST77916_CASET, x, x + w - 1);
    }
    if ((y != _currentY) || (h != _currentH)) {
      _currentY = y;
      _currentH = h;
      y += _yStart;
      _bus->writeC8D16D16(KNOB_ST77916_RASET, y, y + h - 1);
    }
    _bus->writeCommand(KNOB_ST77916_RAMWR);
  }

  void invertDisplay(bool i) override {
    _bus->sendCommand((_ips ^ i) ? KNOB_ST77916_INVON : KNOB_ST77916_INVOFF);
  }

  void displayOn() override {
    _bus->sendCommand(KNOB_ST77916_SLPOUT);
    delay(KNOB_ST77916_SLPOUT_DELAY);
  }

  void displayOff() override {
    _bus->sendCommand(KNOB_ST77916_SLPIN);
    delay(KNOB_ST77916_SLPIN_DELAY);
  }

 protected:
  void tftInit() override {
    if (_rst != GFX_NOT_DEFINED) {
      pinMode(_rst, OUTPUT);
      digitalWrite(_rst, HIGH);
      delay(100);
      digitalWrite(_rst, LOW);
      delay(KNOB_ST77916_RST_DELAY);
      digitalWrite(_rst, HIGH);
      delay(KNOB_ST77916_RST_DELAY);
    } else {
      _bus->sendCommand(KNOB_ST77916_SWRESET);
      delay(KNOB_ST77916_RST_DELAY);
    }
    _bus->batchOperation(kSt77916InitOps, kSt77916InitOpsLen);
    invertDisplay(false);
  }
};

}  // namespace knobify::drivers
