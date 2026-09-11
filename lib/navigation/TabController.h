#pragma once

#include "NavigationStack.h"
#include "ScreenId.h"

namespace knobify::navigation {

enum class Tab { Library, Files };

// Owns the two top-level browsing modes (decision 10 in ADR 0004) as
// separate NavigationStacks, plus which one is active. A left-right
// swipe means "pop" if the active stack can go back, otherwise "switch
// tab" -- swipeLeftRight() implements exactly that rule so InputRouter
// doesn't need to know about tabs at all.
class TabController {
 public:
  TabController()
      : library_(Screen{ScreenKind::Artists, {}}),
        files_(Screen{ScreenKind::Folder, ScreenParams{.folderPath = "/"}}),
        active_(Tab::Library) {}

  NavigationStack &activeStack() {
    return active_ == Tab::Library ? library_ : files_;
  }

  const NavigationStack &activeStack() const {
    return active_ == Tab::Library ? library_ : files_;
  }

  Tab activeTab() const { return active_; }

  void switchTab() {
    active_ = (active_ == Tab::Library) ? Tab::Files : Tab::Library;
  }

  // Pops the active stack if possible, otherwise switches tabs. Returns
  // true if it popped, false if it switched tabs -- callers (e.g. the UI
  // transition animation) use this to pick the right visual treatment.
  bool handleSwipeBack() {
    if (activeStack().canGoBack()) {
      activeStack().pop();
      return true;
    }
    switchTab();
    return false;
  }

 private:
  NavigationStack library_;
  NavigationStack files_;
  Tab active_;
};

}  // namespace knobify::navigation
