#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "ToneSettings.h"

using knobify::playback::KeyValueStore;
using knobify::signal::NoiseColor;
using knobify::signal::ToneParam;
using knobify::signal::ToneSettings;
using knobify::signal::Waveform;

void setUp() {}
void tearDown() {}

namespace {
class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &key, uint8_t &out) override {
    auto it = values.find(key);
    if (it == values.end()) return false;
    out = it->second;
    return true;
  }
  void setU8(const std::string &key, uint8_t value) override { values[key] = value; }
  std::map<std::string, uint8_t> values;
};

// Slow detents: far enough apart that no acceleration applies.
void turnSlowly(ToneSettings &s, int detents, uint32_t &now) {
  const int step = detents > 0 ? 1 : -1;
  for (int i = 0; i != detents; i += step) {
    now += 1000;
    s.turn(step, now);
  }
}

std::string text(const ToneSettings &s, ToneParam p) {
  char buf[24];
  s.valueText(p, buf, sizeof(buf));
  return buf;
}
}  // namespace

void test_defaults() {
  ToneSettings s;
  TEST_ASSERT_EQUAL(Waveform::Sine, s.waveform());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1000.0f, s.frequencyHz());
  TEST_ASSERT_EQUAL_INT(-20, s.levelDb());
  TEST_ASSERT_EQUAL_INT(50, s.dutyPercent());
  TEST_ASSERT_EQUAL_INT(0, s.symmetryPercent());
  TEST_ASSERT_EQUAL(ToneParam::Frequency, s.selected());
}

void test_frequency_moves_on_a_log_grid_and_clamps() {
  ToneSettings s;
  uint32_t now = 0;
  turnSlowly(s, 48, now);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.0f, s.frequencyHz());
  turnSlowly(s, 400, now);
  TEST_ASSERT_EQUAL_INT(ToneSettings::kMaxFrequencyStep, s.frequencyStep());
  TEST_ASSERT_TRUE(s.frequencyHz() <= 20000.0f);
  turnSlowly(s, -600, now);
  TEST_ASSERT_EQUAL_INT(ToneSettings::kMinFrequencyStep, s.frequencyStep());
  TEST_ASSERT_TRUE(s.frequencyHz() >= 20.0f);
}

void test_turning_faster_takes_bigger_steps() {
  ToneSettings s;
  s.turn(1, 1000);  // First turn: slow.
  TEST_ASSERT_EQUAL_INT(1, s.frequencyStep());
  s.turn(1, 1300);  // 300 ms later: slow.
  TEST_ASSERT_EQUAL_INT(2, s.frequencyStep());
  s.turn(1, 1380);  // 80 ms: 12.5 detents/s, mid.
  TEST_ASSERT_EQUAL_INT(6, s.frequencyStep());
  s.turn(1, 1400);  // 20 ms: 50 detents/s, fast.
  TEST_ASSERT_EQUAL_INT(22, s.frequencyStep());
}

void test_level_is_whole_decibels_within_range() {
  ToneSettings s;
  s.select(ToneParam::Level);
  uint32_t now = 0;
  turnSlowly(s, 5, now);
  TEST_ASSERT_EQUAL_INT(-15, s.levelDb());
  turnSlowly(s, 30, now);
  TEST_ASSERT_EQUAL_INT(0, s.levelDb());
  turnSlowly(s, -80, now);
  TEST_ASSERT_EQUAL_INT(-60, s.levelDb());
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.001f, ToneSettings::dbToLinear(-60));
}

void test_the_shape_chip_follows_the_waveform() {
  ToneSettings s;
  TEST_ASSERT_FALSE(ToneSettings::visible(ToneParam::Shape, Waveform::Sine));
  TEST_ASSERT_TRUE(ToneSettings::visible(ToneParam::Shape, Waveform::Square));
  TEST_ASSERT_TRUE(ToneSettings::visible(ToneParam::Shape, Waveform::Saw));
  TEST_ASSERT_FALSE(ToneSettings::visible(ToneParam::Frequency, Waveform::Noise));
  TEST_ASSERT_EQUAL_STRING("Duty", ToneSettings::chipLabel(ToneParam::Shape, Waveform::Square));
  TEST_ASSERT_EQUAL_STRING("Shape", ToneSettings::chipLabel(ToneParam::Shape, Waveform::Saw));
  TEST_ASSERT_EQUAL_STRING("Noise", ToneSettings::chipLabel(ToneParam::Waveform, Waveform::Noise));

  s.select(ToneParam::Shape);  // Not on a sine: ignored.
  TEST_ASSERT_EQUAL(ToneParam::Frequency, s.selected());
}

void test_the_waveform_steps_without_wrapping_and_selection_follows() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 5, now);
  TEST_ASSERT_EQUAL(Waveform::Noise, s.waveform());
  s.select(ToneParam::Frequency);  // Noise has no pitch: ignored.
  TEST_ASSERT_EQUAL(ToneParam::Waveform, s.selected());
  turnSlowly(s, -1, now);
  TEST_ASSERT_EQUAL(Waveform::Saw, s.waveform());
}

void test_duty_and_symmetry_clamp_and_map_to_the_oscillator() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 1, now);  // Square.
  s.select(ToneParam::Shape);
  turnSlowly(s, 60, now);
  TEST_ASSERT_EQUAL_INT(95, s.dutyPercent());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.95f, s.params().shape);

  s.select(ToneParam::Waveform);
  turnSlowly(s, 1, now);  // Saw.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, s.params().shape);  // Rising.
  s.select(ToneParam::Shape);
  turnSlowly(s, 50, now);
  TEST_ASSERT_EQUAL_INT(50, s.symmetryPercent());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, s.params().shape);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, s.params().amplitude);
}

void test_value_text() {
  ToneSettings s;
  TEST_ASSERT_EQUAL_STRING("1.00 kHz", text(s, ToneParam::Frequency).c_str());
  TEST_ASSERT_EQUAL_STRING("-20 dB", text(s, ToneParam::Level).c_str());
  TEST_ASSERT_EQUAL_STRING("Sine", text(s, ToneParam::Waveform).c_str());
  uint32_t now = 0;
  turnSlowly(s, -57, now);  // The grid step nearest A4: 439.0 Hz.
  TEST_ASSERT_EQUAL_STRING("439 Hz", text(s, ToneParam::Frequency).c_str());
  turnSlowly(s, -600, now);
  TEST_ASSERT_EQUAL_STRING("20.3 Hz", text(s, ToneParam::Frequency).c_str());
  turnSlowly(s, 600, now);
  TEST_ASSERT_EQUAL_STRING("19.9 kHz", text(s, ToneParam::Frequency).c_str());

  s.select(ToneParam::Waveform);
  turnSlowly(s, 2, now);  // Saw, symmetry 0.
  TEST_ASSERT_EQUAL_STRING("Rising", text(s, ToneParam::Shape).c_str());
  s.select(ToneParam::Shape);
  turnSlowly(s, 50, now);
  TEST_ASSERT_EQUAL_STRING("Triangle", text(s, ToneParam::Shape).c_str());
  turnSlowly(s, -15, now);
  TEST_ASSERT_EQUAL_STRING("Shape 35%", text(s, ToneParam::Shape).c_str());
}

void test_settings_survive_a_round_trip_and_garbage_is_ignored() {
  FakeStore store;
  ToneSettings a;
  uint32_t now = 0;
  a.select(ToneParam::Waveform);
  turnSlowly(a, 2, now);  // Saw.
  a.select(ToneParam::Frequency);
  turnSlowly(a, -100, now);
  a.select(ToneParam::Level);
  turnSlowly(a, -7, now);
  a.save(store);

  ToneSettings b;
  b.load(store);
  TEST_ASSERT_EQUAL(Waveform::Saw, b.waveform());
  TEST_ASSERT_EQUAL_INT(-100, b.frequencyStep());
  TEST_ASSERT_EQUAL_INT(-27, b.levelDb());

  store.values[ToneSettings::kWaveKey] = 9;
  store.values[ToneSettings::kLevelKey] = 200;
  ToneSettings c;
  c.load(store);
  TEST_ASSERT_EQUAL(Waveform::Sine, c.waveform());
  TEST_ASSERT_EQUAL_INT(-20, c.levelDb());
}

void test_noise_gets_a_colour_chip_that_turns_darker_to_brighter() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 3, now);  // Noise.
  TEST_ASSERT_TRUE(ToneSettings::visible(ToneParam::Shape, Waveform::Noise));
  TEST_ASSERT_EQUAL_STRING("Color", ToneSettings::chipLabel(ToneParam::Shape, Waveform::Noise));
  TEST_ASSERT_EQUAL(NoiseColor::White, s.noiseColor());
  TEST_ASSERT_EQUAL_STRING("White noise", text(s, ToneParam::Shape).c_str());

  s.select(ToneParam::Shape);
  turnSlowly(s, -1, now);
  TEST_ASSERT_EQUAL(NoiseColor::Pink, s.noiseColor());
  turnSlowly(s, -5, now);  // No wrapping.
  TEST_ASSERT_EQUAL(NoiseColor::Brown, s.noiseColor());
  TEST_ASSERT_EQUAL_STRING("Brown noise", text(s, ToneParam::Shape).c_str());
  turnSlowly(s, 3, now);
  TEST_ASSERT_EQUAL(NoiseColor::Blue, s.noiseColor());
  turnSlowly(s, 9, now);
  TEST_ASSERT_EQUAL(NoiseColor::Violet, s.noiseColor());
  TEST_ASSERT_EQUAL(NoiseColor::Violet, s.params().noise);
}

void test_a_fast_turn_never_skips_a_colour() {
  ToneSettings s;
  s.select(ToneParam::Waveform);
  uint32_t now = 0;
  turnSlowly(s, 3, now);
  s.select(ToneParam::Shape);
  turnSlowly(s, -2, now);  // Brown.
  s.turn(1, now + 1000);   // Slow: Pink.
  s.turn(1, now + 1020);   // Fast -- still only one step: White.
  TEST_ASSERT_EQUAL(NoiseColor::White, s.noiseColor());
}

void test_the_noise_colour_survives_a_round_trip() {
  FakeStore store;
  ToneSettings a;
  uint32_t now = 0;
  a.select(ToneParam::Waveform);
  turnSlowly(a, 3, now);
  a.select(ToneParam::Shape);
  turnSlowly(a, -1, now);  // Pink.
  a.save(store);
  ToneSettings b;
  b.load(store);
  TEST_ASSERT_EQUAL(NoiseColor::Pink, b.noiseColor());
  store.values[ToneSettings::kNoiseKey] = 7;
  ToneSettings c;
  c.load(store);
  TEST_ASSERT_EQUAL(NoiseColor::White, c.noiseColor());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults);
  RUN_TEST(test_frequency_moves_on_a_log_grid_and_clamps);
  RUN_TEST(test_turning_faster_takes_bigger_steps);
  RUN_TEST(test_level_is_whole_decibels_within_range);
  RUN_TEST(test_the_shape_chip_follows_the_waveform);
  RUN_TEST(test_the_waveform_steps_without_wrapping_and_selection_follows);
  RUN_TEST(test_duty_and_symmetry_clamp_and_map_to_the_oscillator);
  RUN_TEST(test_value_text);
  RUN_TEST(test_noise_gets_a_colour_chip_that_turns_darker_to_brighter);
  RUN_TEST(test_a_fast_turn_never_skips_a_colour);
  RUN_TEST(test_the_noise_colour_survives_a_round_trip);
  RUN_TEST(test_settings_survive_a_round_trip_and_garbage_is_ignored);
  return UNITY_END();
}
