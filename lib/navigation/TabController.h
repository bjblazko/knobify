#pragma once

#include "NavigationStack.h"
#include "ScreenId.h"

namespace knobify::navigation {

// Menu is the main menu's own stack (Home -> Settings -> Brightness, or
// Now Playing opened from its mini-bar); Library and Files are the two
// music tabs.
enum class Tab { Menu, Library, Files };

// Owns the top-level browsing modes as separate NavigationStacks, plus
// which one is active: the main menu (ADR 0010) and the two swipeable
// music tabs (decision 10 in ADR 0004). A left-right swipe means "pop" if
// the active stack can go back, otherwise "switch music tab" --
// handleSwipeBack() implements exactly that rule so InputRouter doesn't
// need to know about tabs at all.
class TabController {
 public:
  TabController()
      : menu_(Screen{ScreenKind::Home, {}}),
        library_(Screen{ScreenKind::Artists, {}}),
        files_(Screen{ScreenKind::Folder, ScreenParams{.folderPath = "/"}}),
        active_(Tab::Menu),
        lastMusicTab_(Tab::Library) {}

  NavigationStack &activeStack() { return stackFor(active_); }

  const NavigationStack &activeStack() const {
    return const_cast<TabController *>(this)->stackFor(active_);
  }

  Tab activeTab() const { return active_; }
  // The music tab openMusic() returns to.
  Tab lastMusicTab() const { return lastMusicTab_; }

  NavigationStack &stack(Tab tab) { return stackFor(tab); }
  const NavigationStack &stack(Tab tab) const {
    return const_cast<TabController *>(this)->stackFor(tab);
  }

  // Restores which tab is active after a reboot (ADR 0012). A
  // `lastMusicTab` that isn't a music tab is ignored.
  void restoreTabs(Tab active, Tab lastMusicTab) {
    active_ = active;
    if (lastMusicTab != Tab::Menu) lastMusicTab_ = lastMusicTab;
  }

  bool isMusicTab() const { return active_ != Tab::Menu; }

  // Enters Music on whichever tab was used last, with its stack intact.
  void openMusic() { active_ = lastMusicTab_; }

  void goHome() { active_ = Tab::Menu; }

  // Toggles between the two music tabs; does nothing in the menu.
  void switchTab() {
    if (!isMusicTab()) return;
    active_ = (active_ == Tab::Library) ? Tab::Files : Tab::Library;
    lastMusicTab_ = active_;
  }

  // Pops the active stack if possible, otherwise switches music tabs (a
  // no-op on Home). Returns true if it popped, false otherwise -- callers
  // (e.g. the UI transition animation) use this to pick the right visual
  // treatment.
  bool handleSwipeBack() {
    if (activeStack().canGoBack()) {
      activeStack().pop();
      return true;
    }
    switchTab();
    return false;
  }

  // The on-screen back button: pops if possible, otherwise leaves a music
  // tab's root for the main menu.
  void back() {
    if (activeStack().canGoBack()) {
      activeStack().pop();
    } else if (isMusicTab()) {
      goHome();
    }
  }

  // Whether back() would do anything, i.e. whether to show a back button.
  bool canGoBackOrHome() const {
    return activeStack().canGoBack() || isMusicTab();
  }

 private:
  NavigationStack &stackFor(Tab tab) {
    switch (tab) {
      case Tab::Library:
        return library_;
      case Tab::Files:
        return files_;
      default:
        return menu_;
    }
  }

  NavigationStack menu_;
  NavigationStack library_;
  NavigationStack files_;
  Tab active_;
  Tab lastMusicTab_;
};

}  // namespace knobify::navigation
