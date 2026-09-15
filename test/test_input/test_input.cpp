#include <unity.h>

#include <map>

#include "BrightnessSetting.h"
#include "GestureRecognizer.h"
#include "InputRouter.h"
#include "TouchCalibration.h"
#include "TouchLatch.h"

using knobify::input::GestureRecognizer;
using knobify::input::GestureType;
using knobify::input::InputRouter;
using knobify::input::ListMoveSink;
using knobify::input::TouchCalibration;
using knobify::input::TouchLatch;
using knobify::input::TouchSample;
using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::TabController;
using knobify::power::BrightnessSetting;
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
  knobify::playback::SampleWindow readRecentSamples(int16_t *, size_t) override {
    return {};
  }
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
  TabController tabs;
  tabs.openMusic();  // Library/Artists.
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

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
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

  uint8_t before = playback.volume();
  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);  // Not routed to the list.
  TEST_ASSERT_TRUE(playback.volume() == before + 2);
}

void test_encoder_adjusts_brightness_on_brightness_screen() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
  tabs.activeStack().push(Screen{ScreenKind::Brightness, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  brightness.begin();
  InputRouter router(tabs, playback, brightness, sink);

  uint8_t volumeBefore = playback.volume();
  router.onEncoderDelta(-3, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);
  TEST_ASSERT_TRUE(playback.volume() == volumeBefore);
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMaxLevel - 3, brightness.level());
}

void test_encoder_moves_tile_selection_on_home() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;  // Starts on Home.
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

  router.onEncoderDelta(1, 0);

  TEST_ASSERT_EQUAL_INT(1, sink.calls);
}

void test_swipe_pops_when_possible() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openMusic();
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_switches_tab_at_root() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openMusic();
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_tap_is_not_routed_by_input_router() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openMusic();
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  InputRouter router(tabs, playback, brightness, sink);

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

void test_touch_calibration_maps_measured_raw_x_back_to_visual_x() {
  // Raw readings captured on real hardware for crosshairs at x=90/180/270.
  TouchSample left = TouchCalibration::apply({40, 180, true});
  TouchSample center = TouchCalibration::apply({148, 180, true});
  TouchSample right = TouchCalibration::apply({252, 180, true});
  TEST_ASSERT_INT_WITHIN(12, 90, left.x);
  TEST_ASSERT_INT_WITHIN(12, 180, center.x);
  TEST_ASSERT_INT_WITHIN(12, 270, right.x);
  TEST_ASSERT_EQUAL_INT16(180, center.y);
  TEST_ASSERT_TRUE(center.pressed);
}

void test_touch_calibration_clamps_to_screen() {
  TEST_ASSERT_EQUAL_INT16(359, TouchCalibration::apply({4000, 0, true}).x);
  TEST_ASSERT_GREATER_OR_EQUAL(0, TouchCalibration::apply({0, 0, true}).x);
}

void test_touch_latch_reports_press_that_ended_between_reads() {
  TouchLatch latch;
  latch.feed({100, 120, true});
  latch.feed({100, 120, false});
  TouchSample first = latch.read();
  TEST_ASSERT_TRUE(first.pressed);
  TEST_ASSERT_EQUAL_INT16(100, first.x);
  TEST_ASSERT_EQUAL_INT16(120, first.y);
  TEST_ASSERT_FALSE(latch.read().pressed);
}

void test_touch_latch_passes_through_held_press() {
  TouchLatch latch;
  latch.feed({10, 20, true});
  TEST_ASSERT_TRUE(latch.read().pressed);
  latch.feed({12, 22, true});
  TouchSample held = latch.read();
  TEST_ASSERT_TRUE(held.pressed);
  TEST_ASSERT_EQUAL_INT16(12, held.x);
  latch.feed({12, 22, false});
  TEST_ASSERT_FALSE(latch.read().pressed);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_encoder_scrolls_list_on_browse_screen);
  RUN_TEST(test_encoder_adjusts_volume_on_now_playing_screen);
  RUN_TEST(test_encoder_adjusts_brightness_on_brightness_screen);
  RUN_TEST(test_encoder_moves_tile_selection_on_home);
  RUN_TEST(test_swipe_pops_when_possible);
  RUN_TEST(test_swipe_switches_tab_at_root);
  RUN_TEST(test_tap_is_not_routed_by_input_router);
  RUN_TEST(test_gesture_recognizer_detects_tap);
  RUN_TEST(test_gesture_recognizer_detects_swipe_left_to_right);
  RUN_TEST(test_gesture_recognizer_ignores_right_to_left_swipe);
  RUN_TEST(test_gesture_recognizer_ignores_vertical_drag);
  RUN_TEST(test_touch_calibration_maps_measured_raw_x_back_to_visual_x);
  RUN_TEST(test_touch_calibration_clamps_to_screen);
  RUN_TEST(test_touch_latch_reports_press_that_ended_between_reads);
  RUN_TEST(test_touch_latch_passes_through_held_press);
  return UNITY_END();
}
