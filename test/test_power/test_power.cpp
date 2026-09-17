#include <unity.h>

#include "BatteryMonitor.h"
#include "IdleTimer.h"
#include "LockController.h"

using knobify::power::BatteryMonitor;
using knobify::power::IdleTimer;
using knobify::power::LockController;

void setUp() {}
void tearDown() {}

// --- IdleTimer ---

void test_idle_timer_starts_with_display_on() {
  IdleTimer timer;
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

void test_idle_timer_stays_on_before_timeout() {
  IdleTimer timer;
  timer.noteActivity(0);
  TEST_ASSERT_TRUE(timer.tick(IdleTimer::kIdleTimeoutMs - 1));
}

void test_idle_timer_turns_off_after_timeout_with_no_activity() {
  IdleTimer timer;
  timer.noteActivity(0);
  TEST_ASSERT_FALSE(timer.tick(IdleTimer::kIdleTimeoutMs));
}

void test_idle_timer_activity_resets_timeout() {
  IdleTimer timer;
  timer.noteActivity(0);
  timer.noteActivity(IdleTimer::kIdleTimeoutMs - 1);
  TEST_ASSERT_TRUE(timer.tick(2 * IdleTimer::kIdleTimeoutMs - 2));
}

void test_idle_timer_activity_after_off_turns_display_back_on() {
  IdleTimer timer;
  timer.noteActivity(0);
  TEST_ASSERT_FALSE(timer.tick(IdleTimer::kIdleTimeoutMs));
  timer.noteActivity(IdleTimer::kIdleTimeoutMs + 10);
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

// Puts the display to sleep; returns the time it went dark.
static uint32_t sleepDisplay(IdleTimer& timer) {
  timer.noteActivity(0);
  timer.tick(IdleTimer::kIdleTimeoutMs);
  return IdleTimer::kIdleTimeoutMs;
}

void test_idle_timer_single_detents_below_threshold_keep_display_off() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  for (int i = 0; i < IdleTimer::kEncoderWakeDetents - 1; ++i) {
    timer.noteEncoderDelta(1, t += 100, /*requireDeliberateTurn=*/true);
  }
  TEST_ASSERT_FALSE(timer.isDisplayOn());
  timer.noteEncoderDelta(1, t += 100, /*requireDeliberateTurn=*/true);
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

void test_idle_timer_back_and_forth_jiggle_does_not_wake() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  for (int i = 0; i < 10; ++i) {
    timer.noteEncoderDelta(4, t += 50, /*requireDeliberateTurn=*/true);
    timer.noteEncoderDelta(-4, t += 50, /*requireDeliberateTurn=*/true);
  }
  TEST_ASSERT_FALSE(timer.isDisplayOn());
}

void test_idle_timer_pause_resets_encoder_wake_count() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  timer.noteEncoderDelta(IdleTimer::kEncoderWakeDetents - 1, t += 100,
                         /*requireDeliberateTurn=*/true);
  timer.noteEncoderDelta(1, t += IdleTimer::kEncoderWakeWindowMs + 1,
                         /*requireDeliberateTurn=*/true);
  TEST_ASSERT_FALSE(timer.isDisplayOn());
}

void test_idle_timer_batched_quarter_turn_wakes() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  timer.noteEncoderDelta(-IdleTimer::kEncoderWakeDetents, t + 100,
                         /*requireDeliberateTurn=*/true);
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

// Unlocked, the display is merely dimmed on a table: the first detent
// must light it, with no quarter-turn threshold to get past.
void test_idle_timer_single_detent_wakes_when_unlocked() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  timer.noteEncoderDelta(1, t + 100, /*requireDeliberateTurn=*/false);
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

void test_idle_timer_unlocked_wake_does_not_need_window() {
  IdleTimer timer;
  uint32_t t = sleepDisplay(timer);
  timer.noteEncoderDelta(1, t += 100, /*requireDeliberateTurn=*/false);
  timer.tick(t += IdleTimer::kIdleTimeoutMs);
  TEST_ASSERT_FALSE(timer.isDisplayOn());
  timer.noteEncoderDelta(1, t += IdleTimer::kEncoderWakeWindowMs + 1,
                         /*requireDeliberateTurn=*/false);
  TEST_ASSERT_TRUE(timer.isDisplayOn());
}

void test_idle_timer_encoder_detent_resets_timeout_while_on() {
  IdleTimer timer;
  timer.noteActivity(0);
  timer.noteEncoderDelta(1, IdleTimer::kIdleTimeoutMs - 1,
                         /*requireDeliberateTurn=*/false);
  TEST_ASSERT_TRUE(timer.tick(2 * IdleTimer::kIdleTimeoutMs - 2));
}

// --- LockController ---

void test_lock_controller_starts_unlocked() {
  LockController lock;
  TEST_ASSERT_FALSE(lock.isLocked());
}

void test_request_lock_locks() {
  LockController lock;
  lock.requestLock();
  TEST_ASSERT_TRUE(lock.isLocked());
}

void test_hold_and_turn_past_threshold_unlocks() {
  LockController lock;
  lock.requestLock();
  lock.onHoldStart(0);
  lock.onHoldEncoderDelta(LockController::kUnlockDetentThreshold);
  TEST_ASSERT_FALSE(lock.isLocked());
}

void test_releasing_before_threshold_resets_progress_and_stays_locked() {
  LockController lock;
  lock.requestLock();
  lock.onHoldStart(0);
  lock.onHoldEncoderDelta(LockController::kUnlockDetentThreshold - 1);
  lock.onHoldEnd();
  TEST_ASSERT_TRUE(lock.isLocked());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, lock.unlockProgress());
}

void test_encoder_deltas_without_hold_do_not_accumulate() {
  LockController lock;
  lock.requestLock();
  lock.onHoldEncoderDelta(LockController::kUnlockDetentThreshold);
  TEST_ASSERT_TRUE(lock.isLocked());
}

void test_encoder_deltas_after_hold_end_do_not_accumulate() {
  LockController lock;
  lock.requestLock();
  lock.onHoldStart(0);
  lock.onHoldEnd();
  lock.onHoldEncoderDelta(LockController::kUnlockDetentThreshold);
  TEST_ASSERT_TRUE(lock.isLocked());
}

void test_unlock_progress_is_proportional_mid_hold() {
  LockController lock;
  lock.requestLock();
  lock.onHoldStart(0);
  lock.onHoldEncoderDelta(LockController::kUnlockDetentThreshold / 2);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, lock.unlockProgress());
}

void test_negative_deltas_count_toward_progress() {
  LockController lock;
  lock.requestLock();
  lock.onHoldStart(0);
  lock.onHoldEncoderDelta(-LockController::kUnlockDetentThreshold);
  TEST_ASSERT_FALSE(lock.isLocked());
}

// --- BatteryMonitor ---

void test_battery_monitor_at_or_below_empty_reads_zero_percent() {
  BatteryMonitor battery;
  battery.update(BatteryMonitor::kEmptyMilliVolts);
  TEST_ASSERT_EQUAL_INT(0, battery.percent());
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kEmpty);

  battery.update(BatteryMonitor::kEmptyMilliVolts - 200);
  TEST_ASSERT_EQUAL_INT(0, battery.percent());
}

void test_battery_monitor_at_or_above_full_reads_100_percent() {
  BatteryMonitor battery;
  battery.update(BatteryMonitor::kFullMilliVolts);
  TEST_ASSERT_EQUAL_INT(100, battery.percent());
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kFull);

  battery.update(BatteryMonitor::kFullMilliVolts + 500);
  TEST_ASSERT_EQUAL_INT(100, battery.percent());
}

void test_battery_monitor_midpoint_is_about_half() {
  BatteryMonitor battery;
  uint32_t midpoint =
      (BatteryMonitor::kEmptyMilliVolts + BatteryMonitor::kFullMilliVolts) / 2;
  battery.update(midpoint);
  TEST_ASSERT_INT_WITHIN(1, 50, battery.percent());
}

void test_battery_monitor_level_buckets_follow_percent_thresholds() {
  BatteryMonitor battery;
  uint32_t range =
      BatteryMonitor::kFullMilliVolts - BatteryMonitor::kEmptyMilliVolts;

  battery.update(BatteryMonitor::kEmptyMilliVolts + range * 5 / 100);
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kEmpty);

  battery.update(BatteryMonitor::kEmptyMilliVolts + range * 20 / 100);
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kLow);

  battery.update(BatteryMonitor::kEmptyMilliVolts + range * 45 / 100);
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kMedium);

  battery.update(BatteryMonitor::kEmptyMilliVolts + range * 65 / 100);
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kHigh);

  battery.update(BatteryMonitor::kEmptyMilliVolts + range * 90 / 100);
  TEST_ASSERT_TRUE(battery.level() == BatteryMonitor::Level::kFull);
}

void test_battery_monitor_full_cell_voltage_reads_100_percent() {
  BatteryMonitor battery;
  battery.update(4100);
  TEST_ASSERT_EQUAL_INT(100, battery.percent());
  TEST_ASSERT_FALSE(battery.isCharging());
}

void test_battery_monitor_above_4500mv_is_charging() {
  BatteryMonitor battery;
  battery.update(4500);
  TEST_ASSERT_FALSE(battery.isCharging());
  battery.update(4501);
  TEST_ASSERT_TRUE(battery.isCharging());
  battery.update(3900);
  TEST_ASSERT_FALSE(battery.isCharging());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_timer_starts_with_display_on);
  RUN_TEST(test_idle_timer_stays_on_before_timeout);
  RUN_TEST(test_idle_timer_turns_off_after_timeout_with_no_activity);
  RUN_TEST(test_idle_timer_activity_resets_timeout);
  RUN_TEST(test_idle_timer_activity_after_off_turns_display_back_on);
  RUN_TEST(test_idle_timer_single_detents_below_threshold_keep_display_off);
  RUN_TEST(test_idle_timer_back_and_forth_jiggle_does_not_wake);
  RUN_TEST(test_idle_timer_pause_resets_encoder_wake_count);
  RUN_TEST(test_idle_timer_batched_quarter_turn_wakes);
  RUN_TEST(test_idle_timer_single_detent_wakes_when_unlocked);
  RUN_TEST(test_idle_timer_unlocked_wake_does_not_need_window);
  RUN_TEST(test_idle_timer_encoder_detent_resets_timeout_while_on);
  RUN_TEST(test_lock_controller_starts_unlocked);
  RUN_TEST(test_request_lock_locks);
  RUN_TEST(test_hold_and_turn_past_threshold_unlocks);
  RUN_TEST(test_releasing_before_threshold_resets_progress_and_stays_locked);
  RUN_TEST(test_encoder_deltas_without_hold_do_not_accumulate);
  RUN_TEST(test_encoder_deltas_after_hold_end_do_not_accumulate);
  RUN_TEST(test_unlock_progress_is_proportional_mid_hold);
  RUN_TEST(test_negative_deltas_count_toward_progress);
  RUN_TEST(test_battery_monitor_at_or_below_empty_reads_zero_percent);
  RUN_TEST(test_battery_monitor_at_or_above_full_reads_100_percent);
  RUN_TEST(test_battery_monitor_midpoint_is_about_half);
  RUN_TEST(test_battery_monitor_level_buckets_follow_percent_thresholds);
  RUN_TEST(test_battery_monitor_full_cell_voltage_reads_100_percent);
  RUN_TEST(test_battery_monitor_above_4500mv_is_charging);
  return UNITY_END();
}
