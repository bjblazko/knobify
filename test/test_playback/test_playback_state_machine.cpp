#include <unity.h>

#include <map>
#include <vector>

#include "PlaybackStateMachine.h"

using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
using knobify::playback::PlaybackState;
using knobify::playback::PlaybackStateMachine;
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
  void pause() override { running = false; }
  void resume() override { running = true; }
  void stop() override { running = false; }
  void setVolume(uint8_t v) override { lastVolume = v; }
  bool isRunning() override { return running; }
  void loop() override {}

  std::string lastPlayed;
  int playCount = 0;
  uint8_t lastVolume = 255;
  bool running = false;
  bool playSucceeds = true;
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

  sm.play({"/a.mp3", "/b.mp3"}, 1);

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_toggle_play_pause() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0);

  sm.togglePlayPause();
  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Paused);
  TEST_ASSERT_FALSE(driver.running);

  sm.togglePlayPause();
  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_TRUE(driver.running);
}

void test_next_and_prev_move_through_playlist() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3", "/b.mp3", "/c.mp3"}, 0);

  sm.next();
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
  sm.next();
  TEST_ASSERT_EQUAL_STRING("/c.mp3", driver.lastPlayed.c_str());
  sm.next();  // Already at last track -- no-op.
  TEST_ASSERT_EQUAL_STRING("/c.mp3", driver.lastPlayed.c_str());

  sm.prev();
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_track_finished_auto_advances() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3", "/b.mp3"}, 0);

  sm.onTrackFinished();

  TEST_ASSERT_TRUE(sm.state() == PlaybackState::Playing);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", driver.lastPlayed.c_str());
}

void test_track_finished_stops_after_last_track() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine sm(driver, volume);
  sm.begin();
  sm.play({"/a.mp3"}, 0);

  sm.onTrackFinished();

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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_play_starts_playing_selected_track);
  RUN_TEST(test_toggle_play_pause);
  RUN_TEST(test_next_and_prev_move_through_playlist);
  RUN_TEST(test_track_finished_auto_advances);
  RUN_TEST(test_track_finished_stops_after_last_track);
  RUN_TEST(test_volume_clamps_to_bounds);
  RUN_TEST(test_volume_persists_only_after_debounce_settles);
  RUN_TEST(test_loads_persisted_volume_on_begin);
  return UNITY_END();
}
