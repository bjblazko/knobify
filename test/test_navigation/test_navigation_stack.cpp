#include <unity.h>

#include "NavigationStack.h"
#include "TabController.h"

using knobify::collection::CollectionId;

using knobify::navigation::NavigationStack;
using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::Tab;
using knobify::navigation::TabController;

void setUp() {}
void tearDown() {}

void test_root_is_current_on_construction() {
  NavigationStack stack(Screen{ScreenKind::Artists, {}});
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::Artists);
  TEST_ASSERT_FALSE(stack.canGoBack());
}

void test_push_then_pop_returns_to_root() {
  NavigationStack stack(Screen{ScreenKind::Artists, {}});
  stack.push(Screen{ScreenKind::Albums, {}});
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::Albums);
  TEST_ASSERT_TRUE(stack.canGoBack());

  TEST_ASSERT_TRUE(stack.pop());
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::Artists);
  TEST_ASSERT_FALSE(stack.canGoBack());
}

void test_pop_at_root_does_nothing() {
  NavigationStack stack(Screen{ScreenKind::Artists, {}});
  TEST_ASSERT_FALSE(stack.pop());
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::Artists);
}

void test_root_is_injectable() {
  // NavigationStack doesn't hardcode Artists as special -- which is what
  // let the Home menu become a root.
  NavigationStack stack(Screen{ScreenKind::NowPlaying, {}});
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::NowPlaying);
}

void test_push_beyond_max_depth_is_ignored() {
  NavigationStack stack(Screen{ScreenKind::Artists, {}});
  for (std::size_t i = 0; i < NavigationStack::kMaxDepth + 4; ++i) {
    stack.push(Screen{ScreenKind::Albums, {}});
  }
  // Should not have crashed or corrupted state; still at Albums.
  TEST_ASSERT_TRUE(stack.current().kind == ScreenKind::Albums);
}

void test_tab_controller_starts_on_home() {
  TabController tabs;
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Menu);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Home);
  TEST_ASSERT_FALSE(tabs.isBrowseTab());
  TEST_ASSERT_FALSE(tabs.canGoBackOrHome());
}

void test_open_music_starts_on_library_artists_root() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
  TEST_ASSERT_TRUE(tabs.canGoBackOrHome());
}

void test_back_at_music_root_goes_home() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.back();
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Home);
}

void test_back_pops_before_going_home() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  tabs.back();
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_open_music_returns_to_last_used_tab_and_stack() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.switchTab();
  tabs.activeStack().push(Screen{ScreenKind::Folder, {}});
  tabs.goHome();
  tabs.openCollection(knobify::collection::CollectionId::Music);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().canGoBack());
}

void test_swipe_on_home_does_nothing() {
  TabController tabs;
  TEST_ASSERT_FALSE(tabs.handleSwipeBack());
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Menu);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Home);
}

void test_settings_and_brightness_stack_on_the_menu() {
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
  tabs.activeStack().push(Screen{ScreenKind::Brightness, {}});
  tabs.back();
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Settings);
  TEST_ASSERT_TRUE(tabs.handleSwipeBack());
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Home);
}

void test_swipe_back_pops_when_possible() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_TRUE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_back_switches_tab_at_root() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_FALSE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_switch_tab_preserves_each_tabs_own_stack() {
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);

  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Albums);
}

// --- Collections (ADR 0018) ---

void test_each_collection_roots_its_own_stacks() {
  TabController tabs;
  tabs.openCollection(CollectionId::Audiobooks);
  // The tag-browse root carries its collection, so every screen pushed
  // from it inherits which index its ids mean.
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
  TEST_ASSERT_TRUE(tabs.activeStack().current().params.collection ==
                   CollectionId::Audiobooks);
  TEST_ASSERT_TRUE(tabs.activeCollection() == CollectionId::Audiobooks);

  // And the Files root starts inside the collection, not at the card root.
  tabs.switchTab();
  TEST_ASSERT_EQUAL_STRING("/Audiobooks",
                           tabs.activeStack().current().params.folderPath.c_str());
  TEST_ASSERT_TRUE(tabs.activeStack().current().params.collection ==
                   CollectionId::Audiobooks);
}

void test_collections_keep_separate_browse_positions() {
  TabController tabs;
  tabs.openCollection(CollectionId::Audiobooks);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  TEST_ASSERT_EQUAL(2u, tabs.activeStack().depth());

  // Music is untouched by browsing Audiobooks...
  tabs.openCollection(CollectionId::Music);
  TEST_ASSERT_EQUAL(1u, tabs.activeStack().depth());
  TEST_ASSERT_TRUE(tabs.activeStack().current().params.collection ==
                   CollectionId::Music);

  // ...and coming back lands where Audiobooks was left.
  tabs.openCollection(CollectionId::Audiobooks);
  TEST_ASSERT_EQUAL(2u, tabs.activeStack().depth());
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Albums);
}

void test_switching_tabs_never_leaves_the_collection() {
  TabController tabs;
  tabs.openCollection(CollectionId::RadioPlays);
  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeCollection() == CollectionId::RadioPlays);
  TEST_ASSERT_TRUE(tabs.activeStack().current().params.collection ==
                   CollectionId::RadioPlays);
  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeCollection() == CollectionId::RadioPlays);
}

void test_restore_selects_the_collection_before_its_stacks() {
  TabController tabs;
  tabs.restoreTabs(Tab::Library, Tab::Library, CollectionId::RadioPlays);
  TEST_ASSERT_TRUE(tabs.activeCollection() == CollectionId::RadioPlays);
  TEST_ASSERT_TRUE(tabs.activeStack().current().params.collection ==
                   CollectionId::RadioPlays);
}

void test_library_root_follows_the_chosen_browse_axis() {
  TabController tabs;
  tabs.openCollection(CollectionId::Music);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  tabs.setLibraryRoot(ScreenKind::Songs);

  // Re-rooting drops the artist you were inside: it means nothing here.
  TEST_ASSERT_TRUE(tabs.libraryRootKind() == ScreenKind::Songs);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Songs);
  TEST_ASSERT_FALSE(tabs.activeStack().canGoBack());
}

void test_browse_axis_is_per_collection() {
  TabController tabs;
  tabs.openCollection(CollectionId::Music);
  tabs.setLibraryRoot(ScreenKind::Genres);
  tabs.openCollection(CollectionId::Audiobooks);

  TEST_ASSERT_TRUE(tabs.libraryRootKind() == ScreenKind::Artists);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_root_is_current_on_construction);
  RUN_TEST(test_push_then_pop_returns_to_root);
  RUN_TEST(test_pop_at_root_does_nothing);
  RUN_TEST(test_root_is_injectable);
  RUN_TEST(test_push_beyond_max_depth_is_ignored);
  RUN_TEST(test_tab_controller_starts_on_home);
  RUN_TEST(test_open_music_starts_on_library_artists_root);
  RUN_TEST(test_back_at_music_root_goes_home);
  RUN_TEST(test_back_pops_before_going_home);
  RUN_TEST(test_open_music_returns_to_last_used_tab_and_stack);
  RUN_TEST(test_swipe_on_home_does_nothing);
  RUN_TEST(test_settings_and_brightness_stack_on_the_menu);
  RUN_TEST(test_swipe_back_pops_when_possible);
  RUN_TEST(test_swipe_back_switches_tab_at_root);
  RUN_TEST(test_switch_tab_preserves_each_tabs_own_stack);
  RUN_TEST(test_each_collection_roots_its_own_stacks);
  RUN_TEST(test_collections_keep_separate_browse_positions);
  RUN_TEST(test_switching_tabs_never_leaves_the_collection);
  RUN_TEST(test_restore_selects_the_collection_before_its_stacks);
  RUN_TEST(test_library_root_follows_the_chosen_browse_axis);
  RUN_TEST(test_browse_axis_is_per_collection);
  return UNITY_END();
}
