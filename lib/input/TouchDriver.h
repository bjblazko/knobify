#pragma once

#include "GestureRecognizer.h"

namespace knobify::input {

// Hardware-facing surface for the touch controller. Concrete adapter
// (Cst816Driver, lib/drivers-touch/ -- not yet implemented, see ADR 0004
// display/touch bring-up note) reads the CST816 over I2C; this interface
// lets GestureRecognizer/InputRouter be exercised without real hardware.
class TouchDriver {
 public:
  virtual ~TouchDriver() = default;
  // Returns false if there's no new sample since the last poll.
  virtual bool poll(TouchSample &out) = 0;
};

}  // namespace knobify::input
