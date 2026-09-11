#pragma once

#include <cstdint>

namespace knobify::input {

// Hardware-facing surface for the rotary encoder. Concrete adapter
// (GpioEncoderDriver, lib/drivers-encoder/) decodes GPIO7/GPIO8
// quadrature; this interface lets InputRouter's routing logic
// (lib/input/InputRouter.h) be exercised without real hardware.
class EncoderDriver {
 public:
  virtual ~EncoderDriver() = default;
  // Ticks since the last call (signed; positive = clockwise), and resets
  // the internal counter.
  virtual int16_t readDelta() = 0;
};

}  // namespace knobify::input
