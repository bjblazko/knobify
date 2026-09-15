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

}  // namespace

void test_hold_refused_for_unseekable_track() {
  Fixture f("/a.ogg");
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

  f.shuttle.tick(300);

  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());
  TEST_ASSERT_EQUAL_INT32(300, f.driver.seeks[0]);  // (2-1) * 300
  // 300 ms played plus 300 ms jumped: 2×.
  TEST_ASSERT_EQUAL_UINT32(600, f.playback.elapsedMs(300));
}

void test_rewind_step_one_moves_back_at_double_speed() {
  Fixture f;
  f.shuttle.hold(60000);
  f.shuttle.turn(-1, 60000);

  f.shuttle.tick(60300);

  TEST_ASSERT_EQUAL_INT32(-900, f.driver.seeks[0]);  // -(2+1) * 300
  TEST_ASSERT_EQUAL_UINT32(59400, f.playback.elapsedMs(60300));
}

void test_forward_parks_silently_before_the_end() {
  Fixture f;  // 240 s track -> end stop at 239000 ms.
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);

  f.shuttle.tick(238300);

  TEST_ASSERT_EQUAL_INT32(700, f.driver.seeks[0]);
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
  TEST_ASSERT_EQUAL_UINT32(239000, f.playback.elapsedMs(238300));

  f.shuttle.tick(238900);  // Still pushing forward: stays parked.
  TEST_ASSERT_EQUAL_UINT(1, f.driver.seeks.size());
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_turning_away_from_the_stop_resumes() {
  Fixture f;
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);
  f.shuttle.tick(238300);  // Parked.

  f.shuttle.turn(-5, 238400);  // Back to center.
  f.shuttle.tick(238500);

  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Playing);
}

void test_rewind_parks_at_track_start() {
  Fixture f;
  f.shuttle.hold(1000);
  f.shuttle.turn(-5, 1000);

  f.shuttle.tick(1300);

  TEST_ASSERT_EQUAL_INT32(-1300, f.driver.seeks[0]);
  TEST_ASSERT_EQUAL_UINT32(0, f.playback.elapsedMs(1300));
  TEST_ASSERT_TRUE(f.playback.state() == PlaybackState::Paused);
}

void test_release_while_parked_resumes_playing() {
  Fixture f;
  f.shuttle.hold(238000);
  f.shuttle.turn(5, 238000);
  f.shuttle.tick(238300);  // Parked (paused).

  f.shuttle.release(238400);

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

void test_track_change_drops_the_hold() {
  Fixture f;
  f.shuttle.hold(0);
  f.shuttle.turn(3, 0);

  f.playback.next(100);
  f.shuttle.tick(400);

  TEST_ASSERT_FALSE(f.shuttle.isHeld());
  TEST_ASSERT_EQUAL_UINT(0, f.driver.seeks.size());
}

void test_no_jumps_while_duration_unknown() {
  Fixture f;
  f.driver.duration = 0;
  f.shuttle.hold(0);
  f.shuttle.turn(2, 0);

  f.shuttle.tick(300);

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
  return UNITY_END();
}
