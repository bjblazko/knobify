#include <unity.h>

#include <map>
#include <string>

#include "GeneratorControl.h"
#include "ToneSession.h"

using knobify::playback::KeyValueStore;
using knobify::signal::GeneratorControl;
using knobify::signal::GeneratorOutput;
using knobify::signal::OscillatorParams;
using knobify::signal::ToneParam;
using knobify::signal::ToneSession;
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
  void setU8(const std::string &key, uint8_t value) override {
    values[key] = value;
    ++writes;
  }
  std::map<std::string, uint8_t> values;
  int writes = 0;
};

class FakeOutput : public GeneratorOutput {
 public:
  void apply(const OscillatorParams &p) override {
    last = p;
    ++applies;
  }
  void start() override { running = true; }
  void stop() override { running = false; }
  OscillatorParams last;
  int applies = 0;
  bool running = false;
};
}  // namespace

void test_begin_loads_and_applies_but_stays_silent() {
  FakeStore store;
  store.values[ToneSettings::kLevelKey] = 30;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  TEST_ASSERT_EQUAL_INT(-30, session.settings().levelDb());
  TEST_ASSERT_EQUAL_INT(1, out.applies);
  TEST_ASSERT_FALSE(out.running);
  TEST_ASSERT_FALSE(session.running());
}

void test_a_turn_reaches_the_output_at_once() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  TEST_ASSERT_TRUE(session.turn(48, 1000));  // One octave in one go.
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 2000.0f, out.last.frequencyHz);
  TEST_ASSERT_FALSE(session.turn(0, 2000));
}

void test_start_and_stop() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  session.start();
  TEST_ASSERT_TRUE(out.running);
  TEST_ASSERT_TRUE(session.running());
  session.stop();
  TEST_ASSERT_FALSE(out.running);
  TEST_ASSERT_FALSE(session.running());
}

void test_saving_waits_until_the_knob_rests_or_the_tone_stops() {
  FakeStore store;
  FakeOutput out;
  ToneSession session(out, store);
  session.begin();
  session.select(ToneParam::Level);
  session.turn(1, 1000);
  session.turn(1, 1500);
  session.tick(3000);  // 1.5 s after the last turn.
  TEST_ASSERT_EQUAL_INT(0, store.writes);
  session.tick(3500);
  TEST_ASSERT_TRUE(store.writes > 0);
  TEST_ASSERT_EQUAL_UINT8(18, store.values[ToneSettings::kLevelKey]);

  const int before = store.writes;
  session.turn(1, 4000);
  session.stop();
  TEST_ASSERT_TRUE(store.writes > before);
  TEST_ASSERT_EQUAL_UINT8(17, store.values[ToneSettings::kLevelKey]);
}

void test_the_control_hands_parameters_across_intact() {
  GeneratorControl control;
  OscillatorParams p;
  p.waveform = Waveform::Saw;
  p.frequencyHz = 439.61f;
  p.amplitude = 0.001f;
  p.shape = 0.35f;
  control.publish(p);
  control.setRunning(true);
  const OscillatorParams q = control.snapshot();
  TEST_ASSERT_TRUE(control.running());
  TEST_ASSERT_EQUAL(Waveform::Saw, q.waveform);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 439.61f, q.frequencyHz);
  TEST_ASSERT_FLOAT_WITHIN(0.00005f, 0.001f, q.amplitude);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.35f, q.shape);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_loads_and_applies_but_stays_silent);
  RUN_TEST(test_a_turn_reaches_the_output_at_once);
  RUN_TEST(test_start_and_stop);
  RUN_TEST(test_saving_waits_until_the_knob_rests_or_the_tone_stops);
  RUN_TEST(test_the_control_hands_parameters_across_intact);
  return UNITY_END();
}
