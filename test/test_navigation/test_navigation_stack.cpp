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
  // Stands in for swapping in a future Home screen: NavigationStack
  // doesn't hardcode Artists as special.
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

void test_tab_controller_starts_on_library_artists_root() {
  TabController tabs;
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_back_pops_when_possible() {
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_TRUE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Library);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_back_switches_tab_at_root() {
  TabController tabs;

  bool popped = tabs.handleSwipeBack();

  TEST_ASSERT_FALSE(popped);
  TEST_ASSERT_TRUE(tabs.activeTab() == Tab::Files);
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_switch_tab_preserves_each_tabs_own_stack() {
  TabController tabs;
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
  RUN_TEST(test_tab_controller_starts_on_library_artists_root);
  RUN_TEST(test_swipe_back_pops_when_possible);
  RUN_TEST(test_swipe_back_switches_tab_at_root);
  RUN_TEST(test_switch_tab_preserves_each_tabs_own_stack);
  return UNITY_END();
}
