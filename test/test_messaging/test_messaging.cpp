#include <unity.h>

#include "MessageTimer.h"
#include "RoundScreen.h"

using knobify::messaging::MessageScope;
using knobify::messaging::MessageTimer;
using knobify::messaging::visibleWidthAt;

void setUp() {}
void tearDown() {}

void test_shows_until_duration_then_expires_once() {
  MessageTimer timer;
  TEST_ASSERT_FALSE(timer.visible());

  timer.show(1000, 2000, MessageScope::Screen);
  TEST_ASSERT_TRUE(timer.visible());
  TEST_ASSERT_FALSE(timer.expire(2999));
  TEST_ASSERT_TRUE(timer.expire(3000));
  TEST_ASSERT_FALSE(timer.visible());
  TEST_ASSERT_FALSE(timer.expire(4000));  // Reported only once.
}

void test_new_message_restarts_the_timer() {
  MessageTimer timer;
  timer.show(0, 2000, MessageScope::Screen);
  timer.show(1500, 2000, MessageScope::Screen);

  TEST_ASSERT_FALSE(timer.expire(2500));
  TEST_ASSERT_TRUE(timer.expire(3500));
}

void test_expires_across_millis_wraparound() {
  MessageTimer timer;
  timer.show(0xFFFFFF00u, 2000, MessageScope::Screen);

  TEST_ASSERT_FALSE(timer.expire(0x100u));  // 512ms later.
  TEST_ASSERT_TRUE(timer.expire(0x800u));
}

// loop() reads millis() before LVGL handles the tap that shows a message,
// then ticks with that older timestamp -- that must not count as expired.
void test_tick_with_timestamp_from_before_show_does_not_expire() {
  MessageTimer timer;
  timer.show(1005, 2000, MessageScope::Screen);

  TEST_ASSERT_FALSE(timer.expire(1000));
  TEST_ASSERT_TRUE(timer.visible());
}

void test_screen_change_dismisses_only_screen_messages() {
  MessageTimer timer;
  timer.show(0, 2000, MessageScope::Screen);
  TEST_ASSERT_TRUE(timer.dismissScreenMessage());
  TEST_ASSERT_FALSE(timer.visible());

  timer.show(0, 2000, MessageScope::System);
  TEST_ASSERT_FALSE(timer.dismissScreenMessage());
  TEST_ASSERT_TRUE(timer.visible());
}

void test_visible_width_is_widest_at_center_and_shrinks_toward_edges() {
  // 360px circle, a 36px-tall box centered on the middle: the chord at its
  // top/bottom edge (18px off center) is 2*sqrt(180^2-18^2) = 358.
  TEST_ASSERT_EQUAL_INT(358 - 2 * 16, visibleWidthAt(162, 36, 360, 16));
  // Box at y 20..56: its top edge is 160px off center, chord 2*sqrt(180^2-160^2) = 164.
  TEST_ASSERT_EQUAL_INT(164 - 2 * 16, visibleWidthAt(20, 36, 360, 16));
  // Same distance below center gives the same width.
  TEST_ASSERT_EQUAL_INT(visibleWidthAt(20, 36, 360, 16),
                        visibleWidthAt(304, 36, 360, 16));
}

void test_visible_width_is_zero_outside_the_circle() {
  TEST_ASSERT_EQUAL_INT(0, visibleWidthAt(-10, 20, 360, 16));
  TEST_ASSERT_EQUAL_INT(0, visibleWidthAt(355, 20, 360, 16));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_shows_until_duration_then_expires_once);
  RUN_TEST(test_new_message_restarts_the_timer);
  RUN_TEST(test_expires_across_millis_wraparound);
  RUN_TEST(test_tick_with_timestamp_from_before_show_does_not_expire);
  RUN_TEST(test_screen_change_dismisses_only_screen_messages);
  RUN_TEST(test_visible_width_is_widest_at_center_and_shrinks_toward_edges);
  RUN_TEST(test_visible_width_is_zero_outside_the_circle);
  return UNITY_END();
}
