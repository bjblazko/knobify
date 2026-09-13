#include <unity.h>

#include "IdleTimer.h"
#include "LockController.h"

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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_timer_starts_with_display_on);
  RUN_TEST(test_idle_timer_stays_on_before_timeout);
  RUN_TEST(test_idle_timer_turns_off_after_timeout_with_no_activity);
  RUN_TEST(test_idle_timer_activity_resets_timeout);
  RUN_TEST(test_idle_timer_activity_after_off_turns_display_back_on);
  RUN_TEST(test_lock_controller_starts_unlocked);
  RUN_TEST(test_request_lock_locks);
  RUN_TEST(test_hold_and_turn_past_threshold_unlocks);
  RUN_TEST(test_releasing_before_threshold_resets_progress_and_stays_locked);
  RUN_TEST(test_encoder_deltas_without_hold_do_not_accumulate);
  RUN_TEST(test_encoder_deltas_after_hold_end_do_not_accumulate);
  RUN_TEST(test_unlock_progress_is_proportional_mid_hold);
  RUN_TEST(test_negative_deltas_count_toward_progress);
  return UNITY_END();
}
