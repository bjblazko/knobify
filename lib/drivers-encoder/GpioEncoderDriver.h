#pragma once

#include <Arduino.h>

#include "EncoderDriver.h"

namespace knobify::drivers {

// GPIO7/GPIO8 quadrature decoder for the primary MCU's rotation-only
// encoder (no push button -- see device.md, ADR 0004). Interrupt-driven
// on both pins' edges so ticks aren't missed while loop() is busy with
// LVGL rendering; a standard quadrature lookup table resolves direction
// and rejects bounce.
//
// The ISR and everything it calls are defined out-of-line in the .cpp,
// not inline here -- an ESP32/Xtensa linker quirk ("dangerous
// relocation: l32r: literal placed after use") shows up when IRAM_ATTR
// functions are defined inline in a header instead.
//
// Only one instance of this class may exist (the ISR needs a plain
// function pointer and reaches back into the instance via a static
// pointer) -- acceptable since there's exactly one encoder.
class GpioEncoderDriver : public input::EncoderDriver {
 public:
  GpioEncoderDriver(uint8_t pinA, uint8_t pinB) : pinA_(pinA), pinB_(pinB) {}

  void begin();

  int16_t readDelta() override {
    noInterrupts();
    int16_t delta = position_;
    position_ = 0;
    interrupts();
    return delta;
  }

 private:
  // IRAM_ATTR: reachable from the ISR, so must not be evicted to flash
  // (which can be temporarily inaccessible, e.g. during an SD/NVS write)
  // while an edge could fire.
  uint8_t IRAM_ATTR readState() const;
  void IRAM_ATTR handleInterrupt();
  static void IRAM_ATTR isr();

  uint8_t pinA_;
  uint8_t pinB_;
  volatile uint8_t lastState_ = 0;
  volatile int16_t position_ = 0;
  static GpioEncoderDriver *instance_;
};

}  // namespace knobify::drivers
