#pragma once

#include <array>
#include <cstddef>

#include "ScreenId.h"

namespace knobify::navigation {

// A fixed-depth back-stack of screens, constructed with an injected root
// screen. It never hardcodes which ScreenKind is "the" root — that's the
// mechanism that lets a future Home/menu screen be inserted later by
// changing what the caller passes in, not by reworking this class. See
// docs/adr/0004-navigation-library-and-index-architecture.md.
class NavigationStack {
 public:
  // Artists -> Albums -> Tracks -> NowPlaying is the deepest v1 flow;
  // a small fixed bound avoids heap allocation for something this small
  // and bounded.
  static constexpr std::size_t kMaxDepth = 8;

  explicit NavigationStack(Screen root) : depth_(1) { stack_[0] = root; }

  void push(Screen screen) {
    if (depth_ >= kMaxDepth) {
      return;
    }
    stack_[depth_] = screen;
    ++depth_;
  }

  // Returns false (and does nothing) if already at the root.
  bool pop() {
    if (!canGoBack()) {
      return false;
    }
    --depth_;
    return true;
  }

  const Screen &current() const { return stack_[depth_ - 1]; }

  bool canGoBack() const { return depth_ > 1; }

 private:
  std::array<Screen, kMaxDepth> stack_;
  std::size_t depth_;
};

}  // namespace knobify::navigation
