#pragma once

#include "CollectionProfile.h"
#include "NavigationStack.h"
#include "ScreenId.h"

namespace knobify::navigation {

// Menu is the main menu's own stack (Home -> Settings -> Brightness, or
// Now Playing opened from its mini-bar); Library and Files are the two
// browse tabs every collection has.
enum class Tab { Menu, Library, Files };

// Owns the top-level browsing modes as separate NavigationStacks, plus
// which one is active: the main menu (ADR 0010) and, per collection, the
// two swipeable browse tabs (decision 10 in ADR 0004, one set per
// collection since ADR 0018). A left-right swipe means "pop" if the active
// stack can go back, otherwise "switch tab" -- handleSwipeBack()
// implements exactly that rule so InputRouter doesn't need to know about
// tabs at all.
class TabController {
 public:
  TabController()
      : collections_{makeCollectionTabs(collection::CollectionId::Music),
                     makeCollectionTabs(collection::CollectionId::Audiobooks),
                     makeCollectionTabs(collection::CollectionId::RadioPlays)},
        menu_(Screen{ScreenKind::Home, {}}),
        active_(Tab::Menu),
        activeCollection_(collection::CollectionId::Music),
        lastBrowseTab_(Tab::Library) {}

  NavigationStack &activeStack() { return stackFor(active_); }

  const NavigationStack &activeStack() const {
    return const_cast<TabController *>(this)->stackFor(active_);
  }

  Tab activeTab() const { return active_; }
  // The browse tab openCollection() returns to.
  Tab lastBrowseTab() const { return lastBrowseTab_; }
  collection::CollectionId activeCollection() const { return activeCollection_; }

  NavigationStack &stack(Tab tab) { return stackFor(tab); }
  const NavigationStack &stack(Tab tab) const {
    return const_cast<TabController *>(this)->stackFor(tab);
  }

  // Restores which collection and tab are active after a reboot (ADR
  // 0012). A `lastBrowseTab` that isn't a browse tab is ignored.
  void restoreTabs(Tab active, Tab lastBrowseTab,
                   collection::CollectionId collectionId =
                       collection::CollectionId::Music) {
    active_ = active;
    activeCollection_ = collectionId;
    if (lastBrowseTab != Tab::Menu) lastBrowseTab_ = lastBrowseTab;
  }

  bool isBrowseTab() const { return active_ != Tab::Menu; }

  // Enters a collection on whichever tab was used last, with that
  // collection's own stacks intact -- leaving Audiobooks halfway into a
  // series and coming back later lands where you left (ADR 0018).
  void openCollection(collection::CollectionId collectionId) {
    activeCollection_ = collectionId;
    active_ = lastBrowseTab_;
  }

  void goHome() { active_ = Tab::Menu; }

  // Re-roots the active collection's Library tab on another browse axis
  // (ADR 0021). Everything above the root goes: the artist you were
  // inside means nothing on a year or genre shelf.
  void setLibraryRoot(ScreenKind kind) {
    stackFor(Tab::Library).setRoot(
        Screen{kind, ScreenParams{.collection = activeCollection_}});
  }

  ScreenKind libraryRootKind() const {
    return const_cast<TabController *>(this)->stackFor(Tab::Library).at(0).kind;
  }

  // Toggles between the active collection's two browse tabs; does nothing
  // in the menu. Never crosses collections: a swipe is "the other view of
  // what I am browsing", not "a different shelf".
  void switchTab() {
    if (!isBrowseTab()) return;
    active_ = (active_ == Tab::Library) ? Tab::Files : Tab::Library;
    lastBrowseTab_ = active_;
  }

  // Pops the active stack if possible, otherwise switches browse tabs (a
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

  // The on-screen back button: pops if possible, otherwise leaves a browse
  // tab's root for the main menu.
  void back() {
    if (activeStack().canGoBack()) {
      activeStack().pop();
    } else if (isBrowseTab()) {
      goHome();
    }
  }

  // Whether back() would do anything, i.e. whether to show a back button.
  bool canGoBackOrHome() const {
    return activeStack().canGoBack() || isBrowseTab();
  }

 private:
  // One collection's two browse stacks, each rooted inside that
  // collection: the tag-based Library root carries the collection so every
  // screen pushed from it inherits it, and the Files root starts at the
  // collection's own folder rather than the card's root.
  struct CollectionTabs {
    NavigationStack library;
    NavigationStack files;
  };

  static CollectionTabs makeCollectionTabs(collection::CollectionId id) {
    return CollectionTabs{
        NavigationStack(
            Screen{ScreenKind::Artists, ScreenParams{.collection = id}}),
        NavigationStack(Screen{
            ScreenKind::Folder,
            ScreenParams{.folderPath = collection::profileOf(id).rootPath,
                         .collection = id}})};
  }

  NavigationStack &stackFor(Tab tab) {
    CollectionTabs &tabs = collections_[collection::indexOf(activeCollection_)];
    switch (tab) {
      case Tab::Library:
        return tabs.library;
      case Tab::Files:
        return tabs.files;
      default:
        return menu_;
    }
  }

  CollectionTabs collections_[collection::kCollectionCount];
  NavigationStack menu_;
  Tab active_;
  collection::CollectionId activeCollection_;
  Tab lastBrowseTab_;
};

}  // namespace knobify::navigation
