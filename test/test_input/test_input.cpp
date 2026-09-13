#include <unity.h>

#include <map>

#include "GestureRecognizer.h"
#include "InputRouter.h"

using knobify::input::GestureRecognizer;
using knobify::input::GestureType;
using knobify::input::InputRouter;
using knobify::input::ListMoveSink;
using knobify::input::TouchSample;
using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::TabController;
using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
using knobify::playback::PlaybackStateMachine;
using knobify::playback::VolumePersistence;

void setUp() {}
void tearDown() {}

namespace {

class FakeDriver : public PlaybackDriver {
 public:
  bool playFile(const std::string &) override { return true; }
  void pause() override {}
  void resume() override {}
  void stop() override {}
  void setVolume(uint8_t v) override { lastVolume = v; }
  bool isRunning() override { return true; }
  uint32_t durationSeconds() override { return 0; }
  void loop() override {}
  uint8_t lastVolume = 0;
};

class FakeStore : public KeyValueStore {
 public:
  bool getU8(const std::string &, uint8_t &) override { return false; }
  void setU8(const std::string &, uint8_t) override {}
};

class RecordingListSink : public ListMoveSink {
 public:
  void onListMove(int16_t delta) override {
    calls++;
    lastDelta = delta;
  }
  int calls = 0;
  int16_t lastDelta = 0;
};

}  // namespace

void test_encoder_scrolls_list_on_browse_screen() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;  // Starts on Library/Artists.
  RecordingListSink sink;
  InputRouter router(tabs, playback, sink);

  router.onEncoderDelta(3, 0);

  TEST_ASSERT_EQUAL_INT(1, sink.calls);
  TEST_ASSERT_EQUAL_INT16(3, sink.lastDelta);
}

void test_encoder_adjusts_volume_on_now_playing_screen() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::NowPlaying, {}});
  RecordingListSink sink;
  InputRouter router(tabs, playback, sink);

  uint8_t before = playback.volume();
  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);  // Not routed to the list.
  TEST_ASSERT_TRUE(playback.volume() == before + 2);
}

void test_swipe_pops_when_possible() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  RecordingListSink sink;
  InputRouter router(tabs, playback, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_switches_tab_at_root() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  RecordingListSink sink;
  InputRouter router(tabs, playback, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_tap_is_not_routed_by_input_router() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  RecordingListSink sink;
  InputRouter router(tabs, playback, sink);

  router.onGesture({GestureType::Tap, 10, 10});

  // Nothing should have changed -- taps are handled by the UI layer.
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
  TEST_ASSERT_EQUAL_INT(0, sink.calls);
}

void test_gesture_recognizer_detects_tap() {
  GestureRecognizer recognizer;
  recognizer.feed(TouchSample{100, 100, true});
  auto event = recognizer.feed(TouchSample{102, 99, false});

  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_TRUE(event->type == GestureType::Tap);
}

void test_gesture_recognizer_detects_swipe_left_to_right() {
  GestureRecognizer recognizer;
  recognizer.feed(TouchSample{20, 100, true});
  recognizer.feed(TouchSample{60, 102, true});
  auto event = recognizer.feed(TouchSample{100, 101, false});

  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_TRUE(event->type == GestureType::SwipeLeftToRight);
}

void test_gesture_recognizer_ignores_right_to_left_swipe() {
  GestureRecognizer recognizer;
  recognizer.feed(TouchSample{200, 100, true});
  auto event = recognizer.feed(TouchSample{100, 101, false});

  TEST_ASSERT_FALSE(event.has_value());
}

void test_gesture_recognizer_ignores_vertical_drag() {
  GestureRecognizer recognizer;
  recognizer.feed(TouchSample{100, 20, true});
  auto event = recognizer.feed(TouchSample{130, 200, false});

  TEST_ASSERT_FALSE(event.has_value());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_encoder_scrolls_list_on_browse_screen);
  RUN_TEST(test_encoder_adjusts_volume_on_now_playing_screen);
  RUN_TEST(test_swipe_pops_when_possible);
  RUN_TEST(test_swipe_switches_tab_at_root);
  RUN_TEST(test_tap_is_not_routed_by_input_router);
  RUN_TEST(test_gesture_recognizer_detects_tap);
  RUN_TEST(test_gesture_recognizer_detects_swipe_left_to_right);
  RUN_TEST(test_gesture_recognizer_ignores_right_to_left_swipe);
  RUN_TEST(test_gesture_recognizer_ignores_vertical_drag);
  return UNITY_END();
}
