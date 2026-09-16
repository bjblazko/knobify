#include <unity.h>

#include <cstdint>

#include "AudioGain.h"

using knobify::playback::AudioGain;

void setUp() {}
void tearDown() {}

void test_output_gain_scales_without_touching_volume() {
  // The library path: the sample already carries the volume, so only the
  // sleep-timer gain applies.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyOutputGain(1000, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT16(500, AudioGain::applyOutputGain(1000, AudioGain::kUnityOutputGain / 2));
  TEST_ASSERT_EQUAL_INT16(0, AudioGain::applyOutputGain(1000, 0));
  TEST_ASSERT_EQUAL_INT16(-500, AudioGain::applyOutputGain(-1000, AudioGain::kUnityOutputGain / 2));
}

void test_volume_step_21_is_unity_and_step_0_is_silence() {
  // The Vorbis path applies the volume itself.
  TEST_ASSERT_EQUAL_INT16(
      1000, AudioGain::applyVolume(1000, AudioGain::kMaxVolumeStep, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT16(0, AudioGain::applyVolume(1000, 0, AudioGain::kUnityOutputGain));
}

void test_volume_steps_are_monotonic_and_match_the_library_table() {
  // ESP32-audioI2S's volumetable (Audio.h), entry/64 per step.
  const uint8_t expected[22] = {0,  1,  2,  3,  4,  6,  8,  10, 12, 14, 17,
                                20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};
  for (uint8_t step = 0; step <= AudioGain::kMaxVolumeStep; ++step) {
    const float gain = AudioGain::linearGain(step, AudioGain::kUnityOutputGain);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected[step] / 64.0f, gain);
    if (step > 0) {
      TEST_ASSERT_TRUE(gain > AudioGain::linearGain(step - 1, AudioGain::kUnityOutputGain));
    }
  }
}

void test_values_are_clamped_and_steps_beyond_the_table_are_capped() {
  TEST_ASSERT_EQUAL_INT16(32767, AudioGain::applyOutputGain(32767, AudioGain::kUnityOutputGain));
  // Above unity is not allowed: an out-of-range gain must not amplify.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyOutputGain(1000, 60000));
  // Out-of-range volume steps clamp to the loudest entry, never index past it.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyVolume(1000, 200, AudioGain::kUnityOutputGain));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_output_gain_scales_without_touching_volume);
  RUN_TEST(test_volume_step_21_is_unity_and_step_0_is_silence);
  RUN_TEST(test_volume_steps_are_monotonic_and_match_the_library_table);
  RUN_TEST(test_values_are_clamped_and_steps_beyond_the_table_are_capped);
  return UNITY_END();
}
