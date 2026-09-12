#pragma once

#include <Arduino.h>

#include "EncoderDriver.h"

namespace knobify::drivers {

// GPIO7/GPIO8 quadrature decoder for the primary MCU's rotation-only
// encoder (no push button -- see device.md, ADR 0004). Interrupt-driven
// on both pins' edges so ticks aren't missed while loop() is busy with
// LVGL rendering.
//
// Decoding strategy, settled on 2026-09-12 after capturing this
// specific encoder's actual raw pin states on real hardware: this is
// NOT a full 4-state quadrature encoder. A raw log of real turns never
// showed pinState 00 (both contacts closed) even once -- only 11 (both
// open, the true rest state), 01, and 10. Each detent briefly closes
// exactly one contact (-> 01 or -> 10) before returning to rest (11);
// it never closes both. Two earlier decoders assumed a textbook 4-state
// Gray-code cycle (00->01->11->10->00) and, because this hardware never
// produces the 00 state their "click completed" logic keyed off of,
// never fired at all. This decoder instead tracks which single contact
// closed since the last rest state and counts one click when it
// releases back to rest, matching what this hardware actually does.
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
  // How long after counting a step to ignore a newly-closing contact --
  // rejects overshoot at the boundary between two fast consecutive
  // detents without adding any delay to the counting decision itself.
  static constexpr uint32_t kRefractoryMicros = 3000;

  // IRAM_ATTR: reachable from the ISR, so must not be evicted to flash
  // (which can be temporarily inaccessible, e.g. during an SD/NVS write)
  // while an edge could fire.
  uint8_t IRAM_ATTR readState() const;
  void IRAM_ATTR handleInterrupt();
  static void IRAM_ATTR isr();

  enum class PendingContact : uint8_t { kNone, kA, kB };

  uint8_t pinA_;
  uint8_t pinB_;
  // Which single contact (if any) closed since the last time both were
  // seen open (rest, pinState 11) -- see the class comment.
  volatile PendingContact pending_ = PendingContact::kNone;
  volatile uint32_t lastCountMicros_ = 0;
  volatile int16_t position_ = 0;
  static GpioEncoderDriver *instance_;
};

}  // namespace knobify::drivers
