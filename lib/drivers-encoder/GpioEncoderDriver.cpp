#include "GpioEncoderDriver.h"

namespace knobify::drivers {

GpioEncoderDriver *GpioEncoderDriver::instance_ = nullptr;

void GpioEncoderDriver::begin() {
  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);
  instance_ = this;
  lastState_ = readState();
  attachInterrupt(digitalPinToInterrupt(pinA_), &GpioEncoderDriver::isr,
                   CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB_), &GpioEncoderDriver::isr,
                   CHANGE);
}

uint8_t IRAM_ATTR GpioEncoderDriver::readState() const {
  return static_cast<uint8_t>((digitalRead(pinA_) << 1) | digitalRead(pinB_));
}

void IRAM_ATTR GpioEncoderDriver::handleInterrupt() {
  // Standard quadrature direction table, indexed by (previous state << 2
  // | new state); +1/-1 for valid single-step transitions, 0 for
  // bounce/invalid transitions.
  static const int8_t kTransitionTable[16] = {
      0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
  };
  uint8_t newState = readState();
  uint8_t index = static_cast<uint8_t>((lastState_ << 2) | newState);
  position_ = static_cast<int16_t>(position_ + kTransitionTable[index]);
  lastState_ = newState;
}

void IRAM_ATTR GpioEncoderDriver::isr() {
  if (instance_) instance_->handleInterrupt();
}

}  // namespace knobify::drivers
