#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "Spectrum.h"

using knobify::signal::Spectrum;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;
constexpr double kPi = 3.141592653589793;
constexpr size_t kColumns = 160;

std::vector<int16_t> sine(float hz, float amplitude) {
  std::vector<int16_t> s(4096);
  for (size_t i = 0; i < s.size(); ++i) {
    s[i] = static_cast<int16_t>(std::lround(amplitude * std::sin(2.0 * kPi * hz * i / kRate)));
  }
  return s;
}

// 1 kHz is exactly 48 samples a period at 48 kHz, so this square has no
// even harmonics at all.
std::vector<int16_t> square1k() {
  std::vector<int16_t> s(4096);
  for (size_t i = 0; i < s.size(); ++i) s[i] = (i % 48) < 24 ? 16000 : -16000;
  return s;
}

float levelAt(const Spectrum &spectrum, float hz) {
  return spectrum.levels()[spectrum.columnFor(hz)];
}
}  // namespace

void test_columns_span_the_audible_band_on_a_log_axis() {
  Spectrum spectrum(kColumns);
  TEST_ASSERT_EQUAL_size_t(0, spectrum.columnFor(20.0f));
  TEST_ASSERT_EQUAL_size_t(kColumns - 1, spectrum.columnFor(20000.0f));
  // Every octave is the same width.
  const int octaveLow = static_cast<int>(spectrum.columnFor(200.0f)) -
                        static_cast<int>(spectrum.columnFor(100.0f));
  const int octaveHigh = static_cast<int>(spectrum.columnFor(8000.0f)) -
                         static_cast<int>(spectrum.columnFor(4000.0f));
  TEST_ASSERT_INT_WITHIN(1, octaveLow, octaveHigh);
}

void test_a_tone_stands_at_its_level_and_nowhere_else() {
  Spectrum spectrum(kColumns);
  auto s = sine(1000, 3277);  // -20 dBFS.
  spectrum.update(s.data(), s.size(), kRate, 33);
  TEST_ASSERT_FLOAT_WITHIN(1.6f, -20.0f, levelAt(spectrum, 1000));
  TEST_ASSERT_TRUE(levelAt(spectrum, 100) < -60.0f);
  TEST_ASSERT_TRUE(levelAt(spectrum, 10000) < -60.0f);
}

void test_a_square_shows_its_odd_harmonics_only() {
  Spectrum spectrum(kColumns);
  auto s = square1k();
  spectrum.update(s.data(), s.size(), kRate, 33);
  // Fundamental 4/pi of the square's amplitude (16000 = -6.2 dBFS), the
  // third a third of that.
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -4.1f, levelAt(spectrum, 1000));
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -13.6f, levelAt(spectrum, 3000));
  TEST_ASSERT_TRUE(levelAt(spectrum, 2000) < -40.0f);
}

void test_silence_is_the_floor() {
  Spectrum spectrum(kColumns);
  std::vector<int16_t> silent(4096, 0);
  spectrum.update(silent.data(), silent.size(), kRate, 33);
  for (float level : spectrum.levels()) TEST_ASSERT_EQUAL_FLOAT(Spectrum::kFloorDb, level);
}

void test_a_tone_that_stops_falls_away_rather_than_vanishing() {
  Spectrum spectrum(kColumns);
  auto s = sine(1000, 3277);
  spectrum.update(s.data(), s.size(), kRate, 33);
  const float before = levelAt(spectrum, 1000);
  spectrum.update(nullptr, 0, kRate, 100);  // Nothing new for 100 ms.
  TEST_ASSERT_FLOAT_WITHIN(0.01f, before - 100 * Spectrum::kFallDbPerMs,
                           levelAt(spectrum, 1000));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_columns_span_the_audible_band_on_a_log_axis);
  RUN_TEST(test_a_tone_stands_at_its_level_and_nowhere_else);
  RUN_TEST(test_a_square_shows_its_odd_harmonics_only);
  RUN_TEST(test_silence_is_the_floor);
  RUN_TEST(test_a_tone_that_stops_falls_away_rather_than_vanishing);
  return UNITY_END();
}
