#include <unity.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "SpectrumAnalyzer.h"

using knobify::visualizer::SpectrumAnalyzer;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kRate = 44100;
constexpr double kTwoPi = 6.283185307179586;

std::vector<int16_t> sine(double hz, double amplitude01, double gain = 1.0) {
  std::vector<int16_t> out(SpectrumAnalyzer::kFftSize);
  for (size_t i = 0; i < out.size(); ++i) {
    double v = std::sin(kTwoPi * hz * static_cast<double>(i) / kRate) *
               amplitude01 * gain * 32767.0;
    out[i] = static_cast<int16_t>(std::lround(v));
  }
  return out;
}

size_t loudestBand(const SpectrumAnalyzer::Levels &levels) {
  size_t best = 0;
  for (size_t i = 1; i < levels.size(); ++i) {
    if (levels[i] > levels[best]) best = i;
  }
  return best;
}

}  // namespace

void test_silence_gives_all_zero_levels() {
  SpectrumAnalyzer analyzer;
  std::vector<int16_t> silence(SpectrumAnalyzer::kFftSize, 0);
  analyzer.update(silence.data(), silence.size(), kRate, 1.0f, 33);
  for (uint8_t level : analyzer.levels()) TEST_ASSERT_EQUAL_UINT8(0, level);
}

void test_sine_lights_its_own_band_most() {
  SpectrumAnalyzer analyzer;
  auto samples = sine(1000.0, 0.1);
  analyzer.update(samples.data(), samples.size(), kRate, 1.0f, 33);
  auto levels = analyzer.levels();
  size_t band = SpectrumAnalyzer::bandForFrequency(1000.0f);
  TEST_ASSERT_EQUAL_UINT32(band, loudestBand(levels));
  TEST_ASSERT_TRUE(levels[band] > 0);
  // Hann leakage stays local: bands well away from the tone stay dark.
  TEST_ASSERT_EQUAL_UINT8(0, levels[0]);
  TEST_ASSERT_EQUAL_UINT8(0, levels[SpectrumAnalyzer::kBands - 1]);
}

void test_bass_and_treble_land_in_different_bands() {
  TEST_ASSERT_TRUE(SpectrumAnalyzer::bandForFrequency(80.0f) <
                   SpectrumAnalyzer::bandForFrequency(8000.0f));
  TEST_ASSERT_EQUAL_UINT32(0, SpectrumAnalyzer::bandForFrequency(60.0f));
  TEST_ASSERT_EQUAL_UINT32(SpectrumAnalyzer::kBands - 1,
                           SpectrumAnalyzer::bandForFrequency(15000.0f));
}

void test_full_scale_clamps_to_max_level() {
  SpectrumAnalyzer analyzer;
  auto samples = sine(1000.0, 1.0);
  analyzer.update(samples.data(), samples.size(), kRate, 1.0f, 33);
  size_t band = SpectrumAnalyzer::bandForFrequency(1000.0f);
  TEST_ASSERT_EQUAL_UINT8(SpectrumAnalyzer::kMaxLevel, analyzer.levels()[band]);
}

void test_gain_is_divided_out_so_volume_does_not_change_levels() {
  SpectrumAnalyzer loud;
  auto full = sine(440.0, 0.1);
  loud.update(full.data(), full.size(), kRate, 1.0f, 33);

  SpectrumAnalyzer quiet;
  auto attenuated = sine(440.0, 0.1, 0.25);
  quiet.update(attenuated.data(), attenuated.size(), kRate, 0.25f, 33);

  auto a = loud.levels();
  auto b = quiet.levels();
  for (size_t i = 0; i < a.size(); ++i) {
    TEST_ASSERT_INT_WITHIN(1, a[i], b[i]);
  }
}

void test_decay_falls_monotonically_to_zero() {
  SpectrumAnalyzer analyzer;
  auto samples = sine(1000.0, 1.0);
  analyzer.update(samples.data(), samples.size(), kRate, 1.0f, 33);
  size_t band = SpectrumAnalyzer::bandForFrequency(1000.0f);
  uint8_t previous = analyzer.levels()[band];
  for (int i = 0; i < 20; ++i) {
    analyzer.decay(33);
    uint8_t now = analyzer.levels()[band];
    TEST_ASSERT_TRUE(now <= previous);
    previous = now;
  }
  TEST_ASSERT_EQUAL_UINT8(0, previous);
}

void test_quieter_frame_decays_instead_of_dropping_instantly() {
  SpectrumAnalyzer analyzer;
  auto loud = sine(1000.0, 1.0);
  analyzer.update(loud.data(), loud.size(), kRate, 1.0f, 33);
  std::vector<int16_t> silence(SpectrumAnalyzer::kFftSize, 0);
  analyzer.update(silence.data(), silence.size(), kRate, 1.0f, 33);
  size_t band = SpectrumAnalyzer::bandForFrequency(1000.0f);
  TEST_ASSERT_TRUE(analyzer.levels()[band] > 0);
  TEST_ASSERT_TRUE(analyzer.levels()[band] < SpectrumAnalyzer::kMaxLevel);
}

void test_invalid_input_only_decays() {
  SpectrumAnalyzer analyzer;
  auto samples = sine(1000.0, 1.0);
  analyzer.update(samples.data(), samples.size(), 0, 1.0f, 33);
  analyzer.update(samples.data(), samples.size(), kRate, 0.0f, 33);
  analyzer.update(samples.data(), 0, kRate, 1.0f, 33);
  for (uint8_t level : analyzer.levels()) TEST_ASSERT_EQUAL_UINT8(0, level);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_silence_gives_all_zero_levels);
  RUN_TEST(test_sine_lights_its_own_band_most);
  RUN_TEST(test_bass_and_treble_land_in_different_bands);
  RUN_TEST(test_full_scale_clamps_to_max_level);
  RUN_TEST(test_gain_is_divided_out_so_volume_does_not_change_levels);
  RUN_TEST(test_decay_falls_monotonically_to_zero);
  RUN_TEST(test_quieter_frame_decays_instead_of_dropping_instantly);
  RUN_TEST(test_invalid_input_only_decays);
  return UNITY_END();
}
