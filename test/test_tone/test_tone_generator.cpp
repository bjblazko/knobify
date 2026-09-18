#include <unity.h>

#include <cstdint>
#include <cstdlib>

#include "ToneGenerator.h"

using knobify::playback::ToneGenerator;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kRate = 22050;

// Counts sign flips over `samples`, which for a square wave is twice the
// number of full cycles.
int countFlips(ToneGenerator &tone, uint32_t samples, uint32_t rate = kRate) {
  int flips = 0;
  int16_t previous = tone.nextSample(rate);
  for (uint32_t i = 1; i < samples; ++i) {
    const int16_t sample = tone.nextSample(rate);
    if ((sample < 0) != (previous < 0)) ++flips;
    previous = sample;
  }
  return flips;
}
}  // namespace

void test_an_untriggered_generator_is_silent() {
  ToneGenerator tone;
  TEST_ASSERT_FALSE(tone.active());
  for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_INT16(0, tone.nextSample(kRate));
}

void test_a_triggered_tone_is_a_square_wave_at_the_asked_for_pitch() {
  ToneGenerator tone;
  tone.trigger(441, 1000);
  TEST_ASSERT_TRUE(tone.active());

  // One second at 441 Hz is 441 cycles, so 882 sign flips -- allow one
  // either side for where the wave happens to start and stop.
  const int flips = countFlips(tone, kRate);
  TEST_ASSERT_INT_WITHIN(2, 882, flips);
}

void test_the_pitch_follows_the_sample_rate() {
  // The same tone asked for at a different rate must still sound the same.
  ToneGenerator a;
  a.trigger(441, 1000);
  const int atLowRate = countFlips(a, kRate, kRate);

  ToneGenerator b;
  b.trigger(441, 1000);
  const int atHighRate = countFlips(b, 44100, 44100);

  TEST_ASSERT_INT_WITHIN(3, atLowRate, atHighRate);
}

void test_the_amplitude_is_the_full_square() {
  ToneGenerator tone;
  tone.trigger(441, 100);
  bool sawHigh = false, sawLow = false;
  for (int i = 0; i < 2000; ++i) {
    const int16_t sample = tone.nextSample(kRate);
    if (sample == ToneGenerator::kAmplitude) sawHigh = true;
    if (sample == -ToneGenerator::kAmplitude) sawLow = true;
  }
  TEST_ASSERT_TRUE(sawHigh);
  TEST_ASSERT_TRUE(sawLow);
}

void test_a_tone_stops_after_its_duration() {
  ToneGenerator tone;
  tone.trigger(441, 20);  // 20ms at 22050 Hz is 441 samples.
  int sounded = 0;
  for (int i = 0; i < 2000; ++i) {
    if (tone.nextSample(kRate) != 0) ++sounded;
  }
  TEST_ASSERT_INT_WITHIN(2, 441, sounded);
  TEST_ASSERT_FALSE(tone.active());
  TEST_ASSERT_EQUAL_INT16(0, tone.nextSample(kRate));
}

void test_a_retrigger_replaces_the_tone_rather_than_layering() {
  ToneGenerator tone;
  tone.trigger(441, 1000);
  for (int i = 0; i < 100; ++i) tone.nextSample(kRate);

  tone.trigger(882, 20);
  // The new tone's length applies from now, not the old one's.
  int sounded = 0;
  for (int i = 0; i < 2000; ++i) {
    if (tone.nextSample(kRate) != 0) ++sounded;
  }
  TEST_ASSERT_INT_WITHIN(2, 441, sounded);
  TEST_ASSERT_FALSE(tone.active());
}

void test_silence_stops_a_sounding_tone() {
  ToneGenerator tone;
  tone.trigger(441, 1000);
  tone.nextSample(kRate);
  TEST_ASSERT_TRUE(tone.active());

  tone.silence();
  TEST_ASSERT_FALSE(tone.active());
  TEST_ASSERT_EQUAL_INT16(0, tone.nextSample(kRate));
}

void test_a_pending_tone_counts_as_active_before_a_sample_is_taken() {
  // The idle writer decides whether to spin up on exactly this.
  ToneGenerator tone;
  tone.trigger(441, 20);
  TEST_ASSERT_TRUE(tone.active());
}

void test_a_pitchless_trigger_is_ignored() {
  ToneGenerator tone;
  tone.trigger(0, 20);
  TEST_ASSERT_FALSE(tone.active());
  tone.triggerNoise(0, 20, 1000);
  TEST_ASSERT_FALSE(tone.active());
}

void test_a_zero_duration_holds_the_tone_until_silenced() {
  // Gravity's engine lasts as long as the finger does, which no fixed
  // duration can express (ADR 0023).
  ToneGenerator tone;
  tone.trigger(441, 0);
  TEST_ASSERT_TRUE(tone.active());
  int sounded = 0;
  for (int i = 0; i < 10 * static_cast<int>(kRate); ++i) {
    if (tone.nextSample(kRate) != 0) ++sounded;
  }
  TEST_ASSERT_EQUAL_INT(10 * static_cast<int>(kRate), sounded);
  TEST_ASSERT_TRUE(tone.active());

  tone.silence();
  TEST_ASSERT_FALSE(tone.active());
  TEST_ASSERT_EQUAL_INT16(0, tone.nextSample(kRate));
}

void test_noise_is_not_a_tone() {
  // A square wave flips on a fixed schedule; noise does not. Counting the
  // flips is what separates a rocket from a beep.
  ToneGenerator square;
  square.trigger(1400, 1000);
  const int squareFlips = countFlips(square, kRate);

  ToneGenerator hiss;
  hiss.triggerNoise(1400, 1000, ToneGenerator::kAmplitude);
  const int noiseFlips = countFlips(hiss, kRate);

  TEST_ASSERT_INT_WITHIN(4, 2800, squareFlips);
  // Roughly half the shifts flip the sign, so far fewer than the square's
  // every-shift alternation -- and never the same count twice by accident.
  TEST_ASSERT_TRUE(noiseFlips > 0);
  TEST_ASSERT_TRUE(noiseFlips < squareFlips * 3 / 4);
}

void test_noise_never_settles_into_a_pattern_you_could_hum() {
  // A shift register with the wrong taps locks up or repeats quickly; a
  // maximal one does not, which is the whole reason for choosing it.
  ToneGenerator hiss;
  hiss.triggerNoise(11025, 2000, ToneGenerator::kAmplitude);
  int runLength = 1, longestRun = 1;
  int16_t previous = hiss.nextSample(kRate);
  for (int i = 1; i < static_cast<int>(kRate); ++i) {
    const int16_t sample = hiss.nextSample(kRate);
    if ((sample < 0) == (previous < 0)) {
      if (++runLength > longestRun) longestRun = runLength;
    } else {
      runLength = 1;
    }
    previous = sample;
  }
  // Long runs would be audible as a pitch rather than a hiss.
  TEST_ASSERT_TRUE(longestRun < 40);
}

void test_a_quieter_level_is_honoured() {
  // The engine runs continuously and must sit under a blip, which is
  // pitched to cut through music for 24ms.
  ToneGenerator tone;
  tone.trigger(441, 100, ToneGenerator::kAmplitude / 4);
  bool sawQuiet = false;
  for (int i = 0; i < 500; ++i) {
    const int16_t sample = tone.nextSample(kRate);
    if (sample != 0) {
      TEST_ASSERT_TRUE(std::abs(sample) <= ToneGenerator::kAmplitude / 4);
      sawQuiet = true;
    }
  }
  TEST_ASSERT_TRUE(sawQuiet);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_an_untriggered_generator_is_silent);
  RUN_TEST(test_a_triggered_tone_is_a_square_wave_at_the_asked_for_pitch);
  RUN_TEST(test_the_pitch_follows_the_sample_rate);
  RUN_TEST(test_the_amplitude_is_the_full_square);
  RUN_TEST(test_a_tone_stops_after_its_duration);
  RUN_TEST(test_a_retrigger_replaces_the_tone_rather_than_layering);
  RUN_TEST(test_silence_stops_a_sounding_tone);
  RUN_TEST(test_a_pending_tone_counts_as_active_before_a_sample_is_taken);
  RUN_TEST(test_a_pitchless_trigger_is_ignored);
  RUN_TEST(test_a_zero_duration_holds_the_tone_until_silenced);
  RUN_TEST(test_noise_is_not_a_tone);
  RUN_TEST(test_noise_never_settles_into_a_pattern_you_could_hum);
  RUN_TEST(test_a_quieter_level_is_honoured);
  return UNITY_END();
}
