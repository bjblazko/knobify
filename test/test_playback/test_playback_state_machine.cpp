#include <unity.h>

#include <map>
#include <vector>

#include "PlaybackStateMachine.h"

using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
using knobify::playback::PlaybackState;
using knobify::playback::PlaybackStateMachine;
using knobify::playback::RepeatMode;
using knobify::playback::VolumePersistence;

void setUp() {}
void tearDown() {}

namespace {

class FakeDriver : public PlaybackDriver {
 public:
  bool playFile(const std::string &path) override {
    lastPlayed = path;
    playCount++;
    running = true;
    return playSucceeds;
  }
  bool playFileAt(const std::string &path, uint32_t position) override {
    lastPosition = position;
    return playFile(path);
  }
  uint32_t filePosition() override { return position; }
  bool seekByMs(int32_t deltaMs) override {
    seeks.push_back(deltaMs);
    return seekSucceeds;
  }
  void pause() override { running = false; }
  void resume() override { running = true; }
  void stop() override { running = false; }
  void setVolume(uint8_t v) override { lastVolume = v; }
  void setOutputGain(uint16_t gain) override { lastOutputGain = gain; }
  bool isRunning() override { return running; }
  uint32_t durationSeconds() override { return duration; }
  knobify::playback::SampleWindow readRecentSamples(int16_t *, size_t) override {
    return {};
  }
  void loop() override {}

  std::string lastPlayed;
  int playCount = 0;
  uint8_t lastVolume = 255;
  uint16_t lastOutputGain = 0;
  bool running = false;
  bool playSucceeds = true;
  uint32_t duration = 0;
  uint32_t lastPosition = 0;
  uint32_t position = 0;
  std::vector<int32_t> seeks;
  bool seekSucceeds = true;
};

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
    saveCount++;
  }

  std::map<std::string, uint8_t> values;
  int saveCount = 0;
};

}  // namespace

void test_play_starts_playing_selected_track() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.play({"/a.mp3", "/b.mp3"}, 1, 0);

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_duration_unknown_when_stopped_and_passed_through_when_playing() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  driver.duration = 225;

  TEST_ASSERT_EQUAL_UINT32(0, sm.durationSeconds());

  sm.play({"/a.mp3"}, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(225, sm.durationSeconds());
}

void test_toggle_play_pause() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);

  sm.togglePlayPause(0);
  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Paused);
  TEST_ASSERT_FALSE(driver.running);

  sm.togglePlayPause(0);
  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_TRUE(driver.running);
}

void test_next_and_prev_move_through_playlist() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3", "/b.mp3", "/c.mp3"}, 0, 0);

  sm.next(0);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
  sm.next(0);
  TEST_ASSERT_EQUAL_STRING("/c.mp3", driver.lastPlayed.c_str());
  sm.next(0);  // Already at last track -- no-op.
  TEST_ASSERT_EQUAL_STRING("/c.mp3", driver.lastPlayed.c_str());

  sm.prev(0);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_track_finished_auto_advances() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3", "/b.mp3"}, 0, 0);

  sm.onTrackFinished(0);

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_track_finished_stops_after_last_track() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);

  sm.onTrackFinished(0);

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Stopped);
}

void test_stop_releases_the_file_and_keeps_the_queue() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3", "/b.mp3"}, 1, 0);

  sm.stop();

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Stopped);
  TEST_ASSERT_FALSE(driver.running);
  TEST_ASSERT_TRUE(sm.hasQueue());
  sm.stop();  // Already stopped: harmless.
  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Stopped);
}

void test_volume_clamps_to_bounds() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.adjustVolume(-100, 0);
  TEST_ASSERT_EQUAL_UINT8(0, sm.volume());

  sm.adjustVolume(1000, 0);
  TEST_ASSERT_EQUAL_UINT8(21, sm.volume());
}

void test_volume_persists_only_after_debounce_settles() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.adjustVolume(2, 1000);
  sm.tick(1200);  // Too soon -- still within debounce window.
  TEST_ASSERT_EQUAL_INT(0, store.saveCount);

  sm.tick(1000 + PlaybackStateMachine::kVolumeSaveDebounceMs);
  TEST_ASSERT_EQUAL_INT(1, store.saveCount);
}

void test_elapsed_ms_excludes_paused_time() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.play({"/a.mp3"}, 0, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, sm.elapsedMs(1000));
  TEST_ASSERT_EQUAL_UINT32(2000, sm.elapsedMs(3000));

  sm.togglePlayPause(3000);  // Pause at the 2s mark.
  TEST_ASSERT_EQUAL_UINT32(2000, sm.elapsedMs(6000));  // Paused: frozen.

  sm.togglePlayPause(6000);  // Resume after a 3s pause.
  TEST_ASSERT_EQUAL_UINT32(2500, sm.elapsedMs(6500));

  sm.play({"/b.mp3"}, 0, 8000);  // A new track resets the clock.
  TEST_ASSERT_EQUAL_UINT32(0, sm.elapsedMs(8000));
}

void test_elapsed_ms_zero_when_stopped() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  TEST_ASSERT_EQUAL_UINT32(0, sm.elapsedMs(5000));
}

void test_loads_persisted_volume_on_begin() {
  FakeDriver driver;
  FakeStore store;
  store.values[VolumePersistence::kKey] = 15;
  VolumePersistence volume(store);

  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  TEST_ASSERT_EQUAL_UINT8(15, sm.volume());
  TEST_ASSERT_EQUAL_UINT8(15, driver.lastVolume);
}

void test_output_gain_goes_to_the_driver_and_never_touches_volume() {
  FakeDriver driver;
  FakeStore store;
  store.values[VolumePersistence::kKey] = 20;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.setOutputGain(512);
  TEST_ASSERT_EQUAL_UINT16(512, driver.lastOutputGain);
  TEST_ASSERT_EQUAL_UINT8(20, driver.lastVolume);
  TEST_ASSERT_EQUAL_UINT8(20, sm.volume());
  sm.tick(100000);
  TEST_ASSERT_EQUAL_INT(0, store.saveCount);
}

void test_repeat_all_wraps_to_first_track_when_last_finishes() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.setRepeat(RepeatMode::All);

  sm.play({"/a.mp3", "/b.mp3"}, 1, 0);
  sm.onTrackFinished(1000);

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_EQUAL_STRING("/a.mp3", driver.lastPlayed.c_str());
}

void test_repeat_one_restarts_the_finished_track() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.setRepeat(RepeatMode::One);

  sm.play({"/a.mp3", "/b.mp3"}, 0, 0);
  sm.onTrackFinished(5000);

  TEST_ASSERT_EQUAL_INT(2, driver.playCount);
  TEST_ASSERT_EQUAL_STRING("/a.mp3", driver.lastPlayed.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, sm.elapsedMs(5000));  // Clock restarted.
}

void test_cycle_repeat_goes_off_all_one_off() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);

  TEST_ASSERT_TRUE(sm.repeat() == RepeatMode::Off);
  sm.cycleRepeat();
  TEST_ASSERT_TRUE(sm.repeat() == RepeatMode::All);
  sm.cycleRepeat();
  TEST_ASSERT_TRUE(sm.repeat() == RepeatMode::One);
  sm.cycleRepeat();
  TEST_ASSERT_TRUE(sm.repeat() == RepeatMode::Off);
}

void test_shuffle_toggle_keeps_current_track_playing() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.play({"/a.mp3", "/b.mp3", "/c.mp3"}, 1, 0);
  sm.setShuffle(true);

  TEST_ASSERT_TRUE(sm.shuffle());
  TEST_ASSERT_EQUAL_INT(1, driver.playCount);  // No restart.
  TEST_ASSERT_EQUAL_STRING("/b.mp3", sm.currentPath().c_str());
}

void test_play_without_shuffle_turns_shuffle_off() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.play({"/a.mp3", "/b.mp3"}, 0, 0, true);
  TEST_ASSERT_TRUE(sm.shuffle());
  sm.play({"/a.mp3", "/b.mp3"}, 1, 0);

  TEST_ASSERT_FALSE(sm.shuffle());
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_seek_by_moves_elapsed_time_and_driver() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);

  sm.seekBy(5000, 1000);

  TEST_ASSERT_EQUAL_UINT(1, driver.seeks.size());
  TEST_ASSERT_EQUAL_INT32(5000, driver.seeks[0]);
  TEST_ASSERT_EQUAL_UINT32(6000, sm.elapsedMs(1000));

  sm.seekBy(-2000, 1000);
  TEST_ASSERT_EQUAL_UINT32(4000, sm.elapsedMs(1000));
}

void test_seek_by_clamps_at_track_start() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);

  sm.seekBy(-9000, 3000);

  TEST_ASSERT_EQUAL_INT32(-3000, driver.seeks[0]);
  TEST_ASSERT_EQUAL_UINT32(0, sm.elapsedMs(3000));
}

void test_seek_by_keeps_elapsed_when_driver_refuses() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);
  driver.seekSucceeds = false;

  sm.seekBy(5000, 1000);

  TEST_ASSERT_EQUAL_UINT32(1000, sm.elapsedMs(1000));
}

void test_seek_by_ignored_when_stopped() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.seekBy(5000, 1000);

  TEST_ASSERT_EQUAL_UINT(0, driver.seeks.size());
}

void test_can_seek_mp3_m4a_wav_and_ogg_with_a_track() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  TEST_ASSERT_FALSE(sm.canSeek());  // Stopped, nothing queued.

  sm.play({"/a.MP3"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
  sm.play({"/a.wav"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
  sm.play({"/a.m4a"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
  sm.play({"/a.ogg"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
  sm.play({"/a.oga"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
  sm.play({"/a.flac"}, 0, 0);
  TEST_ASSERT_FALSE(sm.canSeek());
  sm.play({"/noextension"}, 0, 0);
  TEST_ASSERT_FALSE(sm.canSeek());
}

void test_track_generation_increments_on_play_next_and_restart() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();

  sm.play({"/a.mp3", "/b.mp3"}, 0, 0);
  uint32_t afterPlay = sm.trackGeneration();
  TEST_ASSERT_TRUE(afterPlay != 0);

  sm.next(0);
  uint32_t afterNext = sm.trackGeneration();
  TEST_ASSERT_TRUE(afterNext != afterPlay);

  sm.setRepeat(RepeatMode::One);
  sm.onTrackFinished(0);  // Restarts the same track (/b.mp3).
  uint32_t afterRestart = sm.trackGeneration();
  TEST_ASSERT_TRUE(afterRestart != afterNext);
}

void test_track_generation_unchanged_by_pause_resume_and_cued_resume() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0, 0);
  uint32_t generation = sm.trackGeneration();

  sm.togglePlayPause(0);  // Pause.
  TEST_ASSERT_EQUAL_UINT32(generation, sm.trackGeneration());
  sm.togglePlayPause(0);  // Resume.
  TEST_ASSERT_EQUAL_UINT32(generation, sm.trackGeneration());

  FakeDriver cuedDriver;
  PlaybackStateMachine cuedSm(cuedDriver, volume);
  cuedSm.begin();
  cuedSm.cue({"/a.mp3"}, 0, false, knobify::playback::PlayScope::File, 1234, 5,
             0);
  uint32_t cuedGeneration = cuedSm.trackGeneration();
  cuedSm.togglePlayPause(0);  // Resumes the cued track -- not a new track.
  TEST_ASSERT_EQUAL_UINT32(cuedGeneration, cuedSm.trackGeneration());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_play_starts_playing_selected_track);
  RUN_TEST(test_duration_unknown_when_stopped_and_passed_through_when_playing);
  RUN_TEST(test_toggle_play_pause);
  RUN_TEST(test_next_and_prev_move_through_playlist);
  RUN_TEST(test_track_finished_auto_advances);
  RUN_TEST(test_track_finished_stops_after_last_track);
  RUN_TEST(test_volume_clamps_to_bounds);
  RUN_TEST(test_volume_persists_only_after_debounce_settles);
  RUN_TEST(test_elapsed_ms_excludes_paused_time);
  RUN_TEST(test_elapsed_ms_zero_when_stopped);
  RUN_TEST(test_loads_persisted_volume_on_begin);
  RUN_TEST(test_output_gain_goes_to_the_driver_and_never_touches_volume);
  RUN_TEST(test_repeat_all_wraps_to_first_track_when_last_finishes);
  RUN_TEST(test_repeat_one_restarts_the_finished_track);
  RUN_TEST(test_cycle_repeat_goes_off_all_one_off);
  RUN_TEST(test_shuffle_toggle_keeps_current_track_playing);
  RUN_TEST(test_play_without_shuffle_turns_shuffle_off);
  RUN_TEST(test_seek_by_moves_elapsed_time_and_driver);
  RUN_TEST(test_seek_by_clamps_at_track_start);
  RUN_TEST(test_seek_by_keeps_elapsed_when_driver_refuses);
  RUN_TEST(test_seek_by_ignored_when_stopped);
  RUN_TEST(test_can_seek_mp3_m4a_wav_and_ogg_with_a_track);
  RUN_TEST(test_stop_releases_the_file_and_keeps_the_queue);
  RUN_TEST(test_track_generation_increments_on_play_next_and_restart);
  RUN_TEST(test_track_generation_unchanged_by_pause_resume_and_cued_resume);
  return UNITY_END();
}
