#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <set>
#include <vector>

#include "Oscillator.h"

using knobify::signal::Oscillator;
using knobify::signal::OscillatorParams;
using knobify::signal::Waveform;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 48000;

OscillatorParams params(Waveform wave, float hz, float amplitude = 1.0f,
                        float shape = 0.5f) {
  OscillatorParams p;
  p.waveform = wave;
  p.frequencyHz = hz;
  p.amplitude = amplitude;
  p.shape = shape;
  return p;
}

// Starts the oscillator and renders past the start ramp, so what follows
// is the steady state.
std::vector<int16_t> steady(Oscillator &osc, size_t count) {
  osc.start();
  std::vector<int16_t> ramp(kRate / 100);  // 10 ms, twice the ramp.
  osc.render(ramp.data(), ramp.size(), kRate);
  std::vector<int16_t> out(count);
  osc.render(out.data(), out.size(), kRate);
  return out;
}

int risingCrossings(const std::vector<int16_t> &s) {
  int n = 0;
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i - 1] < 0 && s[i] >= 0) ++n;
  }
  return n;
}
}  // namespace

void test_a_stopped_oscillator_is_silent_and_idle() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000));
  std::vector<int16_t> out(480, 123);
  osc.render(out.data(), out.size(), kRate);
  TEST_ASSERT_TRUE(osc.idle());
  for (int16_t s : out) TEST_ASSERT_EQUAL_INT16(0, s);
}

void test_a_sine_is_at_the_asked_for_pitch() {
  const float pitches[] = {20.0f, 1000.0f, 15000.0f};
  const int expected[] = {20, 1000, 15000};
  for (int i = 0; i < 3; ++i) {
    Oscillator osc;
    osc.setParams(params(Waveform::Sine, pitches[i]));
    TEST_ASSERT_INT_WITHIN(2, expected[i], risingCrossings(steady(osc, kRate)));
  }
}

void test_the_level_is_the_asked_for_amplitude() {
  Oscillator osc;
  osc.setParams(params(Waveform::Sine, 1000, 0.1f));  // -20 dBFS.
  auto s = steady(osc, 480);
  int peak = 0;
  for (int16_t v : s) peak = std::max(peak, std::abs(static_cast<int>(v)));
  // +-0.5 dB around 3277.
  TEST_ASSERT_INT_WITHIN(190, 3277, peak);
}

void test_a_square_honours_its_duty_cycle() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000, 1.0f, 0.25f));
  auto s = steady(osc, kRate);
  int high = 0;
  for (int16_t v : s) high += v > 0;
  TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, static_cast<float>(high) / s.size());
}

void test_a_saw_rises_falls_or_does_both_by_its_shape() {
  const float shapes[] = {1.0f, 0.5f, 0.0f};
  const float risingShare[] = {0.95f, 0.5f, 0.05f};
  for (int i = 0; i < 3; ++i) {
    Oscillator osc;
    osc.setParams(params(Waveform::Saw, 1000, 1.0f, shapes[i]));
    auto s = steady(osc, kRate);
    int rising = 0;
    for (size_t k = 1; k < s.size(); ++k) rising += s[k] > s[k - 1];
    TEST_ASSERT_FLOAT_WITHIN(0.06f, risingShare[i],
                             static_cast<float>(rising) / (s.size() - 1));
  }
}

void test_noise_is_centred_bounded_and_not_a_pattern() {
  Oscillator osc;
  osc.setParams(params(Waveform::Noise, 1000, 0.5f));
  auto s = steady(osc, kRate);
  long long sum = 0;
  std::set<int16_t> distinct;
  for (int16_t v : s) {
    sum += v;
    distinct.insert(v);
    TEST_ASSERT_TRUE(std::abs(static_cast<int>(v)) <= 16384);
  }
  TEST_ASSERT_TRUE(std::llabs(sum / static_cast<long long>(s.size())) < 300);
  TEST_ASSERT_TRUE(distinct.size() > 1000);
}

void test_a_pitch_change_does_not_jump() {
  Oscillator osc;
  osc.setParams(params(Waveform::Sine, 1000));
  auto before = steady(osc, 100);
  osc.setParams(params(Waveform::Sine, 1500));
  std::vector<int16_t> after(100);
  osc.render(after.data(), after.size(), kRate);
  // The largest step a 1.5 kHz full-scale sine takes between samples,
  // with some room: a restarted phase would jump by up to 2x full scale.
  const int maxStep = 7100;
  TEST_ASSERT_TRUE(std::abs(after[0] - before.back()) < maxStep);
  for (size_t i = 1; i < after.size(); ++i) {
    TEST_ASSERT_TRUE(std::abs(after[i] - after[i - 1]) < maxStep);
  }
}

void test_start_and_stop_ramp_instead_of_clicking() {
  Oscillator osc;
  osc.setParams(params(Waveform::Square, 1000, 1.0f));
  osc.start();
  std::vector<int16_t> start(kRate / 1000);  // 1 ms.
  osc.render(start.data(), start.size(), kRate);
  TEST_ASSERT_TRUE(std::abs(start[0]) < 400);
  TEST_ASSERT_TRUE(std::abs(start.back()) < 32767 / 2);

  osc.stop();
  TEST_ASSERT_FALSE(osc.idle());
  std::vector<int16_t> tail(kRate / 100);  // 10 ms.
  osc.render(tail.data(), tail.size(), kRate);
  TEST_ASSERT_TRUE(osc.idle());
  TEST_ASSERT_EQUAL_INT16(0, tail.back());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_stopped_oscillator_is_silent_and_idle);
  RUN_TEST(test_a_sine_is_at_the_asked_for_pitch);
  RUN_TEST(test_the_level_is_the_asked_for_amplitude);
  RUN_TEST(test_a_square_honours_its_duty_cycle);
  RUN_TEST(test_a_saw_rises_falls_or_does_both_by_its_shape);
  RUN_TEST(test_noise_is_centred_bounded_and_not_a_pattern);
  RUN_TEST(test_a_pitch_change_does_not_jump);
  RUN_TEST(test_start_and_stop_ramp_instead_of_clicking);
  return UNITY_END();
}
