#include <unity.h>

#include <string>

#include "ScopeScale.h"

using knobify::signal::ScopeScale;

void setUp() {}
void tearDown() {}

namespace {
std::string label(const ScopeScale &scale) {
  char buf[32];
  scale.label(buf, sizeof(buf));
  return buf;
}
}  // namespace

void test_doubling_the_pitch_doubles_the_waves_on_screen() {
  ScopeScale scale;
  scale.update(200.0f, -20);
  TEST_ASSERT_EQUAL_UINT32(10000, scale.timebaseUs());  // 2 periods.
  scale.update(400.0f, -20);
  TEST_ASSERT_EQUAL_UINT32(10000, scale.timebaseUs());  // Still: 4 periods.
}

void test_the_timebase_steps_once_the_screen_gets_crowded() {
  ScopeScale scale;
  scale.update(200.0f, -20);
  scale.update(800.0f, -20);  // 8 periods would be too many.
  TEST_ASSERT_EQUAL_UINT32(5000, scale.timebaseUs());
}

void test_the_timebase_does_not_flap_at_a_boundary() {
  ScopeScale scale;
  scale.update(500.0f, -20);
  TEST_ASSERT_EQUAL_UINT32(5000, scale.timebaseUs());
  scale.update(350.0f, -20);  // 1.75 periods: inside the hysteresis.
  TEST_ASSERT_EQUAL_UINT32(5000, scale.timebaseUs());
  scale.update(300.0f, -20);  // 1.5: now it moves.
  TEST_ASSERT_EQUAL_UINT32(10000, scale.timebaseUs());
}

void test_the_ends_of_the_band() {
  ScopeScale low;
  low.update(20.26f, -20);
  TEST_ASSERT_EQUAL_UINT32(50000, low.timebaseUs());  // The longest step.
  ScopeScale high;
  high.update(19870.0f, -20);
  TEST_ASSERT_EQUAL_UINT32(200, high.timebaseUs());
}

void test_raising_the_level_by_six_db_doubles_the_height() {
  ScopeScale scale;
  scale.update(1000.0f, -26);
  TEST_ASSERT_EQUAL_INT(-20, scale.topDb());
  scale.update(1000.0f, -20);
  TEST_ASSERT_EQUAL_INT(-20, scale.topDb());
}

void test_the_level_range_steps_in_ten_db_with_hysteresis() {
  ScopeScale scale;
  scale.update(1000.0f, -20);
  scale.update(1000.0f, -19);  // Above the top: must step up.
  TEST_ASSERT_EQUAL_INT(-10, scale.topDb());
  scale.update(1000.0f, -21);  // Only 11 dB under the top: stays.
  TEST_ASSERT_EQUAL_INT(-10, scale.topDb());
  scale.update(1000.0f, -23);  // 13 dB under: steps down.
  TEST_ASSERT_EQUAL_INT(-20, scale.topDb());
  scale.update(1000.0f, -60);
  TEST_ASSERT_EQUAL_INT(-60, scale.topDb());
  scale.update(1000.0f, 0);
  TEST_ASSERT_EQUAL_INT(0, scale.topDb());
}

void test_the_label_names_both_scales() {
  ScopeScale scale;
  scale.update(800.0f, -20);
  TEST_ASSERT_EQUAL_STRING("5 ms \xC2\xB7 -20 dB", label(scale).c_str());
  scale.update(19870.0f, 0);
  TEST_ASSERT_EQUAL_STRING("200 \xC2\xB5s \xC2\xB7 0 dB", label(scale).c_str());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_doubling_the_pitch_doubles_the_waves_on_screen);
  RUN_TEST(test_the_timebase_steps_once_the_screen_gets_crowded);
  RUN_TEST(test_the_timebase_does_not_flap_at_a_boundary);
  RUN_TEST(test_the_ends_of_the_band);
  RUN_TEST(test_raising_the_level_by_six_db_doubles_the_height);
  RUN_TEST(test_the_level_range_steps_in_ten_db_with_hysteresis);
  RUN_TEST(test_the_label_names_both_scales);
  return UNITY_END();
}
