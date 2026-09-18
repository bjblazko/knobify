#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "TriggeredScope.h"

using knobify::signal::TriggeredScope;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;
constexpr double kPi = 3.141592653589793;
constexpr size_t kWindow = 4096;
constexpr size_t kWidth = 160;

std::vector<int16_t> sine(float hz, float amplitude, size_t offset = 0) {
  std::vector<int16_t> s(kWindow);
  for (size_t i = 0; i < kWindow; ++i) {
    s[i] = static_cast<int16_t>(std::lround(
        amplitude * std::sin(2.0 * kPi * hz * (i + offset) / kRate)));
  }
  return s;
}

std::vector<int16_t> trace(const std::vector<int16_t> &s, float hint) {
  std::vector<int16_t> out(kWidth);
  TriggeredScope::trace(s.data(), s.size(), kRate, hint, out.data(), kWidth);
  return out;
}
}  // namespace

void test_a_sine_starts_on_its_rising_zero_crossing() {
  auto out = trace(sine(1000, 10000, 13), 1000);
  // Two periods across 160 points: one period is ~80 points.
  TEST_ASSERT_TRUE(std::abs(out[0]) < 400);
  TEST_ASSERT_TRUE(out[20] > 9500);
  TEST_ASSERT_TRUE(out[60] < -9500);
}

void test_the_picture_stands_still_between_windows() {
  auto a = trace(sine(1000, 10000, 0), 1000);
  auto b = trace(sine(1000, 10000, 37), 1000);
  for (size_t i = 0; i < kWidth; ++i) TEST_ASSERT_INT_WITHIN(250, a[i], b[i]);
}

void test_without_a_hint_the_period_is_estimated() {
  auto out = trace(sine(1000, 10000, 13), 0.0f);
  TEST_ASSERT_TRUE(std::abs(out[0]) < 400);
  TEST_ASSERT_TRUE(out[20] > 9000);
}

void test_silence_is_the_zero_line() {
  std::vector<int16_t> silent(kWindow, 0);
  for (int16_t v : trace(silent, 1000)) TEST_ASSERT_EQUAL_INT16(0, v);
}

void test_a_period_longer_than_the_window_still_draws() {
  auto out = trace(sine(20, 10000), 20);
  bool moved = false;
  for (int16_t v : out) {
    TEST_ASSERT_TRUE(std::abs(v) <= 10000);
    moved |= v != out[0];
  }
  TEST_ASSERT_TRUE(moved);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_sine_starts_on_its_rising_zero_crossing);
  RUN_TEST(test_the_picture_stands_still_between_windows);
  RUN_TEST(test_without_a_hint_the_period_is_estimated);
  RUN_TEST(test_silence_is_the_zero_line);
  RUN_TEST(test_a_period_longer_than_the_window_still_draws);
  return UNITY_END();
}
