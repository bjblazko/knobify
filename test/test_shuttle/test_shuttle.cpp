#include <unity.h>

#include <map>
#include <vector>

#include "PlaybackStateMachine.h"
#include "Shuttle.h"

using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
using knobify::playback::PlaybackState;
using knobify::playback::PlaybackStateMachine;
using knobify::playback::Shuttle;
using knobify::playback::VolumePersistence;

void setUp() {}
void tearDown() {}

namespace {

class FakeDriver : public PlaybackDriver {
 public:
  bool playFile(const std::string &) override { return true; }
  bool playFileAt(const std::string &, uint32_t) override { return true; }
  uint32_t filePosition() override { return 0; }
  bool seekByMs(int32_t deltaMs) override {
    seeks.push_back(deltaMs);
    return true;
  }
  void pause() override {}
  void resume() override {}
  void stop() override {}
  void setVolume(uint8_t) override {}
  void setOutputGain(uint16_t) override {}
  bool isRunning() override { return true; }
  uint32_t durationSeconds() override { return duration; }
  knobify::playback::SampleWindow readRecentSamples(int16_t *, size_t) override {
    return {};
  }
  void loop() override {}

  std::vector<int32_t> seeks;
  uint32_t duration = 240;
};

class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &, uint8_t &) override { return false; }
  void setU8(const std::string &, uint8_t) override {}
};

// One playing 4-minute track, started at t=0.
struct Fixture {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume{store};
  PlaybackStateMachine playback{driver, volume};
  Shuttle shuttle{playback};

  explicit Fixture(const char *path = "/a.mp3") {
    playback.begin();
    playback.play({path, "/b.mp3"}, 0, 0);
  }
};

// Timings below are in cue cycles, so tuning kCycleMs (tuned by ear on the
// device, ADR 0013) doesn't rewrite every expectation.
constexpr uint32_t C = Shuttle::kCycleMs;

}  // namespace

void test_hold_refused_for_unseekable_track() {
  Fixture f("/a.flac");
  TEST_ASSERT_FALSE(f.shuttle.hold(0));
  TEST_ASSERT_FALSE(f.shuttle.isHeld());
}

void test_turn_ignored_unless_held() {
  Fixture f;
  f.shuttle.turn(2, 0);
  TEST_ASSERT_EQUAL_INT8(0, f.shuttle.step());
}

void test_turn_clamps_at_end_stops() {
  Fixture f;
  f.shuttle.hold(0);
  f.shuttle.turn(9, 0);
  TEST_ASSERT_EQUAL_INT8(5, f.shuttle.step());
  f.shuttle.turn(-20, 0);
  TEST_ASSERT_EQUAL_INT8(-5, f.shuttle.step());
}

// At step 0 tick() restarts the cycle clock, so the first jump after
// leaving center comes a full cycle later.
void test_no_jump_at_center_or_before_a_cycle_passes() {
  Fixture f;
  f.shuttle.hold(0);
  f.shuttle.tick(1000);  // Step 0: normal play, no jumps.
  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());

  f.shuttle.turn(1, 1000);
  f.shuttle.tick(1100);  // Only 100 ms since the tick at 1000.
  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());
}

void test_forward_step_one_doubles_speed() {
  Fixture f;
  f.shuttle.hold(0);
  f.shuttle.turn(1, 0);

  f.shuttle.tick(C);

  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());
  TEST_ASSERT_EQUAL_INT32(C, f.driver.seeks[0]);  // (2-1) * C
  // One cycle played plus one cycle jumped: 2×.
  TEST_ASSERT_EQUAL_UINT32(2 * C, f.playback.elapsedMs(C));
}

void test_rewind_step_one_moves_back_at_double_speed() {
  Fixture f;
  f.shuttle.hold(60000);
  f.shuttle.turn(-1, 60000);

  f.shuttle.tick(60000 + C);

  TEST_ASSERT_EQUAL_INT32(-3 * static_cast<int32_t>(C), f.driver.seeks[0]);  // -(2+1) * C
  TEST_ASSERT_EQUAL_UINT32(60000 - 2 * C, f.playback.elapsedMs(60000 + C));
}

void test_forward_parks_silently_before_the_end() {
  Fixture f;  // 240 s track -> end stop at 239000 ms.
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);

  f.shuttle.tick(238000 + C);

  TEST_ASSERT_EQUAL_INT32(239000 - (238000 + C), f.driver.seeks[0]);
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
  TEST_ASSERT_EQUAL_UINT32(239000, f.playback.elapsedMs(238000 + C));

  f.shuttle.tick(238000 + 3 * C);  // Still pushing forward: stays parked.
  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_turning_away_from_the_stop_resumes() {
  Fixture f;
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);
  f.shuttle.tick(238000 + C);  // Parked.

  f.shuttle.turn(-5, 238100 + C);  // Back to center.
  f.shuttle.tick(238200 + C);

  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Playing);
}

void test_rewind_parks_at_track_start() {
  Fixture f;
  f.shuttle.hold(1000);
  f.shuttle.turn(-5, 1000);

  f.shuttle.tick(1000 + C);

  TEST_ASSERT_EQUAL_INT32(-static_cast<int32_t>(1000 + C), f.driver.seeks[0]);
  TEST_ASSERT_EQUAL_UINT32(0, f.playback.elapsedMs(1000 + C));
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_release_while_parked_resumes_playing() {
  Fixture f;
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);
  f.shuttle.tick(238000 + C);  // Parked (paused).

  f.shuttle.release(238100 + C);

  TEST_ASSERT_FALSE(f.shuttle.isHeld());
  TEST_ASSERT_EQUAL_INT8(0, f.shuttle.step());
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Playing);
}

void test_hold_from_pause_plays_cue_and_release_pauses_again() {
  Fixture f;
  f.playback.togglePlayPause(1000);  // Paused at 1000 ms.

  TEST_ASSERT_TRUE(f.shuttle.hold(2000));
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Playing);

  f.shuttle.release(2500);
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_release_while_parked_at_start_resumes_playing() {
  Fixture f;
  f.shuttle.hold(1000);
  f.shuttle.turn(-5, 1000);
  f.shuttle.tick(1000 + C);  // Parked at the start stop (paused).
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);

  f.shuttle.release(1100 + C);

  TEST_ASSERT_FALSE(f.shuttle.isHeld());
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Playing);
}

void test_shrinking_step_that_keeps_its_sign_stays_parked() {
  Fixture f;
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);
  f.shuttle.tick(238000 + C);  // Parked at the end stop.
  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());

  f.shuttle.turn(-3, 238100 + C);  // Step +5 -> +2: still pushing forward.
  TEST_ASSERT_EQUAL_INT8(2, f.shuttle.step());
  f.shuttle.tick(238000 + 3 * C);

  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());  // No new seek.
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_consecutive_cycles_accumulate_up_to_the_end_stop() {
  Fixture f;  // 240 s track -> end stop at 239000 ms.
  f.shuttle.hold(230000);
  f.shuttle.turn(2, 230000);  // Step +2: speed 4x, jump (4-1)*C per cycle.

  f.shuttle.tick(230000 + C);  // One cycle played + 3 cycles jumped.
  TEST_ASSERT_EQUAL_INT32(3 * C, f.driver.seeks[0]);
  TEST_ASSERT_EQUAL_UINT32(230000 + 4 * C, f.playback.elapsedMs(230000 + C));

  f.shuttle.tick(230000 + 2 * C);  // Another 4 cycles' worth.
  TEST_ASSERT_EQUAL_INT32(3 * C, f.driver.seeks[1]);
  TEST_ASSERT_EQUAL_UINT32(230000 + 8 * C, f.playback.elapsedMs(230000 + 2 * C));

  // Keep cycling until the end stop is reached.
  uint32_t nowMs = 230000 + 2 * C;
  while (f.playback.state() == PlaybackState::Playing) {
    nowMs += C;
    f.shuttle.tick(nowMs);
  }

  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
  TEST_ASSERT_EQUAL_UINT32(239000, f.playback.elapsedMs(nowMs));
}

void test_track_change_drops_the_hold() {
  Fixture f;
  f.shuttle.hold(0);
  f.shuttle.turn(3, 0);

  f.playback.next(100);
  f.shuttle.tick(100 + C);

  TEST_ASSERT_FALSE(f.shuttle.isHeld());
  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());
}

void test_repeat_one_restart_of_same_path_drops_the_hold() {
  Fixture f;
  f.playback.setRepeat(knobify::playback::RepeatMode::One);
  f.shuttle.hold(0);
  f.shuttle.turn(3, 0);

  f.playback.onTrackFinished(100);  // Restarts /a.mp3 -- same path, new track.
  f.shuttle.tick(100 + C);

  TEST_ASSERT_FALSE(f.shuttle.isHeld());
  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());
}

void test_hold_on_cued_track_then_tick_stays_held() {
  FakeDriver cuedDriver;
  FakeStore cuedStore;
  VolumePersistence cuedVolume{cuedStore};
  PlaybackStateMachine cuedSm{cuedDriver, cuedVolume};
  Shuttle cuedShuttle{cuedSm};
  cuedSm.begin();
  cuedSm.cue({"/a.mp3"}, 0, false, knobify::playback::PlayScope::File, 1234, 5,
             0);

  TEST_ASSERT_TRUE(cuedShuttle.hold(2000));  // hold() resumes the cued track.
  cuedShuttle.tick(2100);

  TEST_ASSERT_TRUE(cuedShuttle.isHeld());
}

void test_no_jumps_while_duration_unknown() {
  Fixture f;
  f.driver.duration = 0;
  f.shuttle.hold(0);
  f.shuttle.turn(2, 0);

  f.shuttle.tick(C);

  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_hold_refused_for_unseekable_track);
  RUN_TEST(test_turn_ignored_unless_held);
  RUN_TEST(test_turn_clamps_at_end_stops);
  RUN_TEST(test_no_jump_at_center_or_before_a_cycle_passes);
  RUN_TEST(test_forward_step_one_doubles_speed);
  RUN_TEST(test_rewind_step_one_moves_back_at_double_speed);
  RUN_TEST(test_forward_parks_silently_before_the_end);
  RUN_TEST(test_turning_away_from_the_stop_resumes);
  RUN_TEST(test_rewind_parks_at_track_start);
  RUN_TEST(test_release_while_parked_resumes_playing);
  RUN_TEST(test_hold_from_pause_plays_cue_and_release_pauses_again);
  RUN_TEST(test_track_change_drops_the_hold);
  RUN_TEST(test_no_jumps_while_duration_unknown);
  RUN_TEST(test_repeat_one_restart_of_same_path_drops_the_hold);
  RUN_TEST(test_hold_on_cued_track_then_tick_stays_held);
  RUN_TEST(test_release_while_parked_at_start_resumes_playing);
  RUN_TEST(test_shrinking_step_that_keeps_its_sign_stays_parked);
  RUN_TEST(test_consecutive_cycles_accumulate_up_to_the_end_stop);
  return UNITY_END();
}
