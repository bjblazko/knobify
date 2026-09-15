#include <unity.h>

#include "NavigationStack.h"
#include "TabController.h"

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
  TEST_ASSERT_FALSE(tabs.isMusicTab());
  TEST_ASSERT_FALSE(tabs.canGoBackOrHome());
}

void test_open_music_starts_on_library_artists_root() {
  TabController tabs;
  tabs.openMusic();
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
  TEST_ASSERT_TRUE(tabs.canGoBackOrHome());
}

void test_back_at_music_root_goes_home() {
  TabController tabs;
  tabs.openMusic();
  tabs.back();
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Home);
}

void test_back_pops_before_going_home() {
  TabController tabs;
  tabs.openMusic();
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  tabs.back();
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_open_music_returns_to_last_used_tab_and_stack() {
  TabController tabs;
  tabs.openMusic();
  tabs.switchTab();
  tabs.activeStack().push(Screen{ScreenKind::Folder, {}});
  tabs.goHome();
  tabs.openMusic();
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
  tabs.openMusic();
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_TRUE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_back_switches_tab_at_root() {
  TabController tabs;
  tabs.openMusic();

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_FALSE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_switch_tab_preserves_each_tabs_own_stack() {
  TabController tabs;
  tabs.openMusic();
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);

  tabs.switchTab();
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Albums);
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
  return UNITY_END();
}
