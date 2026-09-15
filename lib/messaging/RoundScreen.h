#pragma once

#include <cmath>

namespace knobify::messaging {

// Widest a horizontally centered box spanning rows [top, top + height) can
// be while staying inside a round screen of `diameter` px, minus `margin`
// on each side -- the chord at the box's edge farthest from the center.
// 0 if that edge is outside the circle.
inline int visibleWidthAt(int top, int height, int diameter, int margin) {
  double radius = diameter / 2.0;
  double topOffset = std::fabs(top - radius);
  double bottomOffset = std::fabs(top + height - radius);
  double offset = topOffset > bottomOffset ? topOffset : bottomOffset;
  if (offset >= radius) return 0;
  int width = static_cast<int>(2.0 * std::sqrt(radius * radius - offset * offset)) -
              2 * margin;
  return width > 0 ? width : 0;
}

}  // namespace knobify::messaging
