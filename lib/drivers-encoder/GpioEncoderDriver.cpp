#include "GpioEncoderDriver.h"

namespace knobify::drivers {

GpioEncoderDriver *GpioEncoderDriver::instance_ = nullptr;

namespace {
constexpr uint8_t kRest = 0b11;
constexpr uint8_t kAClosed = 0b01;
constexpr uint8_t kBClosed = 0b10;
}  // namespace

void GpioEncoderDriver::begin() {
  pinMode(pinA_, INPUT_PULLUP);
  pinMode(pinB_, INPUT_PULLUP);
  instance_ = this;
  attachInterrupt(digitalPinToInterrupt(pinA_), &GpioEncoderDriver::isr,
                   CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB_), &GpioEncoderDriver::isr,
                   CHANGE);
}

uint8_t IRAM_ATTR GpioEncoderDriver::readState() const {
  return static_cast<uint8_t>((digitalRead(pinA_) << 1) | digitalRead(pinB_));
}

void IRAM_ATTR GpioEncoderDriver::handleInterrupt() {
  uint8_t s = readState();
  if (s == kRest) {
    // A detent completed if exactly one contact closed and released --
    // count it now, using whichever contact led. If neither contact
    // closed (spurious double-edge) there's nothing to count.
    if (pending_ == PendingContact::kA) {
      // Confirmed against real hardware feedback 2026-09-12: contact A
      // leading (this sign) is clockwise, which is what a user expects
      // to make it louder.
      position_ = static_cast<int16_t>(position_ + 1);
      lastCountMicros_ = micros();
    } else if (pending_ == PendingContact::kB) {
      position_ = static_cast<int16_t>(position_ - 1);
      lastCountMicros_ = micros();
    }
    pending_ = PendingContact::kNone;
  } else if (s == kAClosed) {
    // The brief refractory window after a just-counted step rejects
    // mechanical overshoot at the boundary between two consecutive fast
    // detents (turning quickly can dip briefly through the *other*
    // contact right as the previous one releases) -- without it, a
    // continuous fast turn would occasionally register one stray step
    // backwards. Found from real hardware feedback 2026-09-12.
    if (pending_ == PendingContact::kNone &&
        micros() - lastCountMicros_ >= kRefractoryMicros) {
      pending_ = PendingContact::kA;
    }
  } else if (s == kBClosed) {
    if (pending_ == PendingContact::kNone &&
        micros() - lastCountMicros_ >= kRefractoryMicros) {
      pending_ = PendingContact::kB;
    }
  }
  // s == 0 (both contacts closed) has never been observed on this
  // hardware; if it ever occurs, treat it as ambiguous and ignore --
  // pending_ is left as whatever it already was.
}

void IRAM_ATTR GpioEncoderDriver::isr() {
  if (instance_) instance_->handleInterrupt();
}

}  // namespace knobify::drivers
