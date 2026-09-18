#include <unity.h>

#include "MenuVisibility.h"

using knobify::navigation::MenuVisibility;
using ToggleResult = MenuVisibility::ToggleResult;

void setUp() {}
void tearDown() {}

namespace {

// The shape of the real menu table: six entries, Settings (index 3)
// pinned -- see kMenuEntries in ScreenManagerMenu.cpp. That table is
// append-only, so this count grows at the end and the pinned bit stays
// put; a mismatch here means the two have drifted apart.
constexpr int kEntries = 6;
constexpr uint8_t kSettingsPinned = 1u << 3;

MenuVisibility makeMenu() { return MenuVisibility(kEntries, kSettingsPinned); }

}  // namespace

void test_everything_shows_before_the_user_changes_anything() {
  auto menu = makeMenu();
  TEST_ASSERT_EQUAL(kEntries, menu.visibleCount());
  for (int i = 0; i < kEntries; ++i) TEST_ASSERT_TRUE(menu.visible(i));
}

void test_hiding_an_entry_hides_only_that_one() {
  auto menu = makeMenu();
  TEST_ASSERT_EQUAL(static_cast<int>(ToggleResult::Toggled),
                    static_cast<int>(menu.toggle(2)));
  TEST_ASSERT_FALSE(menu.visible(2));
  TEST_ASSERT_TRUE(menu.visible(1));
  TEST_ASSERT_TRUE(menu.visible(3));
  TEST_ASSERT_EQUAL(kEntries - 1, menu.visibleCount());
}

void test_toggling_twice_puts_it_back() {
  auto menu = makeMenu();
  const uint8_t before = menu.mask();
  menu.toggle(1);
  menu.toggle(1);
  TEST_ASSERT_EQUAL_UINT8(before, menu.mask());
  TEST_ASSERT_TRUE(menu.visible(1));
}

void test_a_pinned_entry_can_never_be_hidden() {
  auto menu = makeMenu();
  TEST_ASSERT_EQUAL(static_cast<int>(ToggleResult::Pinned),
                    static_cast<int>(menu.toggle(3)));
  TEST_ASSERT_TRUE(menu.visible(3));
  // Even a stored mask with the bit cleared -- a record from an older
  // build, or a corrupt byte -- must not hide it.
  menu.setMask(0x00);
  TEST_ASSERT_TRUE(menu.visible(3));
  TEST_ASSERT_EQUAL(1, menu.visibleCount());
}

void test_the_last_visible_entry_cannot_be_hidden() {
  // An unpinned menu is the case the pinned rule would not cover.
  MenuVisibility menu(kEntries, 0);
  for (int i = 0; i < kEntries - 1; ++i) menu.toggle(i);
  TEST_ASSERT_EQUAL(1, menu.visibleCount());
  TEST_ASSERT_EQUAL(static_cast<int>(ToggleResult::WouldEmptyMenu),
                    static_cast<int>(menu.toggle(kEntries - 1)));
  TEST_ASSERT_EQUAL(1, menu.visibleCount());
}

void test_a_hidden_entry_can_always_be_shown_again() {
  MenuVisibility menu(kEntries, 0);
  for (int i = 0; i < kEntries - 1; ++i) menu.toggle(i);
  TEST_ASSERT_EQUAL(static_cast<int>(ToggleResult::Toggled),
                    static_cast<int>(menu.toggle(0)));
  TEST_ASSERT_EQUAL(2, menu.visibleCount());
}

void test_entries_outside_the_table_are_inert() {
  auto menu = makeMenu();
  const uint8_t before = menu.mask();
  TEST_ASSERT_FALSE(menu.visible(-1));
  TEST_ASSERT_FALSE(menu.visible(kEntries));
  menu.toggle(-1);
  menu.toggle(kEntries);
  menu.toggle(99);
  TEST_ASSERT_EQUAL_UINT8(before, menu.mask());
  TEST_ASSERT_EQUAL(kEntries, menu.visibleCount());
}

void test_a_stored_mask_round_trips() {
  auto menu = makeMenu();
  menu.toggle(0);
  menu.toggle(4);
  const uint8_t saved = menu.mask();

  auto restored = makeMenu();
  restored.setMask(saved);
  TEST_ASSERT_FALSE(restored.visible(0));
  TEST_ASSERT_TRUE(restored.visible(1));
  TEST_ASSERT_FALSE(restored.visible(4));
  TEST_ASSERT_EQUAL(menu.visibleCount(), restored.visibleCount());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_everything_shows_before_the_user_changes_anything);
  RUN_TEST(test_hiding_an_entry_hides_only_that_one);
  RUN_TEST(test_toggling_twice_puts_it_back);
  RUN_TEST(test_a_pinned_entry_can_never_be_hidden);
  RUN_TEST(test_the_last_visible_entry_cannot_be_hidden);
  RUN_TEST(test_a_hidden_entry_can_always_be_shown_again);
  RUN_TEST(test_entries_outside_the_table_are_inert);
  RUN_TEST(test_a_stored_mask_round_trips);
  return UNITY_END();
}
