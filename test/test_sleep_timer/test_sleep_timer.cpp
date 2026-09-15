#include <unity.h>

#include "SleepTimer.h"

using knobify::power::SleepPhase;
using knobify::power::SleepTimer;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kMin = 60000;

}  // namespace

void test_starts_off() {
  SleepTimer timer;
  TEST_ASSERT_TRUE(timer.tick(0) == SleepPhase::Off);
  TEST_ASSERT_FALSE(timer.isActive());
  TEST_ASSERT_EQUAL_UINT32(0, timer.remainingMs(0));
  TEST_ASSERT_EQUAL_UINT16(SleepTimer::kUnityGain, timer.fadeGain(0));
}

void test_steps_up_through_presets_and_clamps_at_the_top() {
  SleepTimer timer;
  const uint32_t expected[] = {15, 30, 45, 60, 90, 120, 120};
  for (uint32_t minutes : expected) {
    timer.step(1, 1000);
    TEST_ASSERT_EQUAL_UINT32(minutes * kMin, timer.remainingMs(1000));
  }
}

void test_steps_down_to_off_and_clamps_there() {
  SleepTimer timer;
  timer.step(3, 0);  // 45
  timer.step(-1, 0);
  TEST_ASSERT_EQUAL_UINT32(30 * kMin, timer.remainingMs(0));
  timer.step(-5, 0);
  TEST_ASSERT_FALSE(timer.isActive());
  timer.step(-1, 0);
  TEST_ASSERT_FALSE(timer.isActive());
}

void test_steps_from_the_remaining_time_not_the_chosen_preset() {
  SleepTimer timer;
  timer.step(2, 0);  // 30 min
  uint32_t now = 3 * kMin;  // 27 min left.
  timer.step(1, now);
  TEST_ASSERT_EQUAL_UINT32(30 * kMin, timer.remainingMs(now));

  SleepTimer other;
  other.step(2, 0);     // 30 min.
  other.step(-1, now);  // 27 left -> 15.
  TEST_ASSERT_EQUAL_UINT32(15 * kMin, other.remainingMs(now));
}

void test_a_just_set_preset_is_not_picked_again() {
  SleepTimer timer;
  timer.step(2, 0);  // 30 min.
  timer.step(1, 500);  // A few hundred ms later: still means "next".
  TEST_ASSERT_EQUAL_UINT32(45 * kMin, timer.remainingMs(500));
  timer.step(-1, 1000);
  TEST_ASSERT_EQUAL_UINT32(30 * kMin, timer.remainingMs(1000));
}

void test_remaining_minutes_round_up() {
  SleepTimer timer;
  timer.step(1, 0);  // 15 min.
  TEST_ASSERT_EQUAL_UINT32(15, timer.remainingMinutesCeil(0));
  TEST_ASSERT_EQUAL_UINT32(15, timer.remainingMinutesCeil(1));
  TEST_ASSERT_EQUAL_UINT32(14, timer.remainingMinutesCeil(kMin));
  TEST_ASSERT_EQUAL_UINT32(1, timer.remainingMinutesCeil(15 * kMin - 1));
}

void test_runs_then_fades_then_expires() {
  SleepTimer timer;
  timer.step(1, 0);
  uint32_t fadeStart = 15 * kMin - SleepTimer::kFadeMs;
  TEST_ASSERT_TRUE(timer.tick(fadeStart - 1) == SleepPhase::Running);
  TEST_ASSERT_EQUAL_UINT16(SleepTimer::kUnityGain, timer.fadeGain(fadeStart - 1));
  TEST_ASSERT_TRUE(timer.tick(fadeStart + 1) == SleepPhase::Fading);
  // Cubic, so loudness falls evenly: halfway through, 1/8 amplitude (-18 dB).
  uint16_t half = timer.fadeGain(fadeStart + SleepTimer::kFadeMs / 2);
  TEST_ASSERT_UINT16_WITHIN(2, SleepTimer::kUnityGain / 8, half);
  // Never louder later in the fade.
  uint16_t previous = SleepTimer::kUnityGain;
  for (uint32_t t = fadeStart; t <= 15 * kMin; t += 100) {
    uint16_t gain = timer.fadeGain(t);
    TEST_ASSERT_TRUE(gain <= previous);
    previous = gain;
  }
  TEST_ASSERT_TRUE(timer.tick(15 * kMin) == SleepPhase::Expired);
  TEST_ASSERT_EQUAL_UINT16(0, timer.fadeGain(15 * kMin));
  TEST_ASSERT_TRUE(timer.tick(20 * kMin) == SleepPhase::Expired);
}

void test_cancel_turns_it_off() {
  SleepTimer timer;
  timer.step(1, 0);
  timer.tick(15 * kMin - 5000);
  timer.cancel();
  TEST_ASSERT_TRUE(timer.tick(15 * kMin) == SleepPhase::Off);
  TEST_ASSERT_EQUAL_UINT16(SleepTimer::kUnityGain, timer.fadeGain(15 * kMin));
}

void test_survives_millis_wraparound() {
  SleepTimer timer;
  uint32_t start = 0xFFFFFFFFu - kMin;
  timer.step(1, start);
  uint32_t later = start + 5 * kMin;  // Wrapped.
  TEST_ASSERT_TRUE(later < start);
  TEST_ASSERT_TRUE(timer.tick(later) == SleepPhase::Running);
  TEST_ASSERT_EQUAL_UINT32(10 * kMin, timer.remainingMs(later));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_off);
  RUN_TEST(test_steps_up_through_presets_and_clamps_at_the_top);
  RUN_TEST(test_steps_down_to_off_and_clamps_there);
  RUN_TEST(test_steps_from_the_remaining_time_not_the_chosen_preset);
  RUN_TEST(test_a_just_set_preset_is_not_picked_again);
  RUN_TEST(test_remaining_minutes_round_up);
  RUN_TEST(test_runs_then_fades_then_expires);
  RUN_TEST(test_cancel_turns_it_off);
  RUN_TEST(test_survives_millis_wraparound);
  return UNITY_END();
}
