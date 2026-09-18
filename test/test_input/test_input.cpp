#include <unity.h>

#include <functional>
#include <map>
#include <vector>

#include "BlobStore.h"
#include "BrightnessSetting.h"
#include "GestureRecognizer.h"
#include "InputRouter.h"
#include "Shuttle.h"
#include "SleepTimer.h"
#include "TouchCalibration.h"
#include "TouchCalibrator.h"
#include "TouchLatch.h"

using knobify::input::CalibrationOutcome;
using knobify::input::CalibrationPhase;
using knobify::input::TouchCalibrationFlow;
using knobify::input::TouchCalibrator;
using knobify::resume::BlobStore;
using knobify::input::GestureRecognizer;
using knobify::input::GestureType;
using knobify::input::InputRouter;
using knobify::input::KnobSink;
using knobify::input::TouchCalibration;
using knobify::input::TouchLatch;
using knobify::input::TouchSample;
using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::TabController;
using knobify::power::BrightnessSetting;
using knobify::power::SleepTimer;
using knobify::playback::KeyValueStore;
using knobify::playback::PlaybackDriver;
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
  bool seekByMs(int32_t) override { return true; }
  void pause() override {}
  void resume() override {}
  void stop() override {}
  void setVolume(uint8_t v) override { lastVolume = v; }
  void setOutputGain(uint16_t) override {}
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

class RecordingListSink : public KnobSink {
 public:
  void onListMove(int16_t delta) override {
    calls++;
    lastDelta = delta;
  }
  void onPaddleMove(int16_t delta) override {
    paddleCalls++;
    lastPaddleDelta = delta;
  }
  int calls = 0;
  int16_t lastDelta = 0;
  int paddleCalls = 0;
  int16_t lastPaddleDelta = 0;
};

class FakeBlobStore : public BlobStore {
 public:
  bool getBlob(const std::string &key, std::vector<uint8_t> &out) override {
    auto it = data.find(key);
    if (it == data.end()) return false;
    out = it->second;
    return true;
  }
  bool setBlob(const std::string &key, const std::vector<uint8_t> &value) override {
    data[key] = value;
    return true;
  }
  void removeBlob(const std::string &key) override { data.erase(key); }
  std::map<std::string, std::vector<uint8_t>> data;
};

// Taps each raw point in turn, well apart in time; returns the time after.
uint32_t feedTaps(const std::function<void(const TouchSample &, uint32_t)> &feed,
                  std::initializer_list<TouchCalibrator::Point> taps) {
  uint32_t now = 1000;
  for (const auto &p : taps) {
    feed({p.x, p.y, true}, now);
    now += 40;
    feed({p.x, p.y, true}, now);
    now += 80;
    feed({p.x, p.y, false}, now);
    now += 500;
  }
  return now;
}

uint32_t feedTaps(TouchCalibrator &calibrator,
                  std::initializer_list<TouchCalibrator::Point> taps) {
  calibrator.reset(0);
  return feedTaps(
      [&](const TouchSample &s, uint32_t now) { calibrator.feed(s, now); }, taps);
}

uint32_t feedTaps(TouchCalibrationFlow &flow,
                  std::initializer_list<TouchCalibrator::Point> taps) {
  return feedTaps(
      [&](const TouchSample &s, uint32_t now) { flow.feedRaw(s, now); }, taps);
}

}  // namespace

void test_encoder_scrolls_list_on_browse_screen() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);  // Library/Artists.
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

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
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  uint8_t before = playback.volume();
  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);  // Not routed to the list.
  TEST_ASSERT_TRUE(playback.volume() == before + 2);
}

void test_encoder_shuttles_instead_of_volume_while_held() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  playback.play({"/a.mp3"}, 0, 0);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::NowPlaying, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  uint8_t before = playback.volume();
  TEST_ASSERT_TRUE(shuttle.hold(0));
  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT8(2, shuttle.step());
  TEST_ASSERT_TRUE(playback.volume() == before);
  TEST_ASSERT_EQUAL_INT(0, sink.calls);

  shuttle.release(0);
  router.onEncoderDelta(1, 0);
  TEST_ASSERT_TRUE(playback.volume() == before + 1);
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
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  uint8_t volumeBefore = playback.volume();
  router.onEncoderDelta(-3, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);
  TEST_ASSERT_TRUE(playback.volume() == volumeBefore);
  TEST_ASSERT_EQUAL_UINT8(BrightnessSetting::kMaxLevel - 3, brightness.level());
}

void test_encoder_sets_sleep_timer_on_sleep_screen() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::SleepTimer, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT(0, sink.calls);
  TEST_ASSERT_EQUAL_UINT32(30u * 60000u, sleepTimer.remainingMs(0));
}

void test_encoder_moves_tile_selection_on_home() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;  // Starts on Home.
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onEncoderDelta(1, 0);

  TEST_ASSERT_EQUAL_INT(1, sink.calls);
}

void test_swipe_pops_when_possible() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  tabs.activeStack().push(Screen{ScreenKind::Albums, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Artists);
}

void test_swipe_switches_tab_at_root() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onGesture({GestureType::SwipeLeftToRight, 0, 0});

  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Folder);
}

void test_tap_is_not_routed_by_input_router() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.openCollection(knobify::collection::CollectionId::Music);
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

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
  const TouchCalibration cal = TouchCalibration::defaults();
  TouchSample left = cal.apply({40, 180, true});
  TouchSample center = cal.apply({148, 180, true});
  TouchSample right = cal.apply({252, 180, true});
  TEST_ASSERT_INT_WITHIN(12, 90, left.x);
  TEST_ASSERT_INT_WITHIN(12, 180, center.x);
  TEST_ASSERT_INT_WITHIN(12, 270, right.x);
  TEST_ASSERT_EQUAL_INT16(180, center.y);
  TEST_ASSERT_TRUE(center.pressed);
}

void test_touch_calibration_clamps_to_screen() {
  const TouchCalibration cal = TouchCalibration::defaults();
  TEST_ASSERT_EQUAL_INT16(359, cal.apply({4000, 0, true}).x);
  TEST_ASSERT_GREATER_OR_EQUAL(0, cal.apply({0, 0, true}).x);
}

void test_touch_calibration_blob_round_trips() {
  TouchCalibration cal{1120, -40, 980, 7};
  auto decoded = TouchCalibration::decode(cal.encode());
  TEST_ASSERT_TRUE(decoded.has_value());
  TEST_ASSERT_TRUE(*decoded == cal);
}

void test_touch_calibration_rejects_corrupt_blob() {
  auto blob = TouchCalibration::defaults().encode();
  auto wrongVersion = blob;
  wrongVersion[0] = 99;
  TEST_ASSERT_FALSE(TouchCalibration::decode(wrongVersion).has_value());
  TEST_ASSERT_FALSE(
      TouchCalibration::decode({blob.begin(), blob.end() - 1}).has_value());
  TouchCalibration absurd{100, 0, 1000, 0};  // Scale 0.1.
  TEST_ASSERT_FALSE(TouchCalibration::decode(absurd.encode()).has_value());
}

void test_calibrator_fits_measured_skew() {
  TouchCalibrator calibrator;
  // Raw points the 2026-09-13 skew (raw x ~= 1.183 * visual - 66, y as is)
  // gives for the four targets, with a few px of finger scatter.
  feedTaps(calibrator, {{147, 72}, {280, 178}, {150, 293}, {16, 181}});
  auto fit = calibrator.fit();
  TEST_ASSERT_TRUE(fit.has_value());
  TEST_ASSERT_INT_WITHIN(30, 1183, fit->xScaleMilli);
  TEST_ASSERT_INT_WITHIN(8, -66, fit->xOffset);
  TEST_ASSERT_INT_WITHIN(30, 1000, fit->yScaleMilli);
  TEST_ASSERT_INT_WITHIN(8, 0, fit->yOffset);
}

void test_calibrator_rejects_taps_on_one_spot() {
  TouchCalibrator calibrator;
  feedTaps(calibrator, {{180, 180}, {180, 180}, {180, 180}, {180, 180}});
  TEST_ASSERT_FALSE(calibrator.fit().has_value());
}

void test_calibrator_rejects_swapped_targets() {
  TouchCalibrator calibrator;
  // Top and bottom crosses tapped the wrong way round.
  feedTaps(calibrator, {{147, 290}, {280, 180}, {150, 70}, {16, 180}});
  TEST_ASSERT_FALSE(calibrator.fit().has_value());
}

void test_calibrator_ignores_press_right_after_start() {
  TouchCalibrator calibrator;
  calibrator.reset(0);
  calibrator.feed({180, 180, true}, 50);
  calibrator.feed({180, 180, false}, 80);
  TEST_ASSERT_EQUAL_UINT(0, calibrator.targetsDone());
}

void test_calibration_flow_keeps_confirmed_fit_and_persists_it() {
  FakeBlobStore blobs;
  TouchCalibrationFlow flow(blobs);
  flow.begin();
  flow.start(0);
  feedTaps(flow, {{147, 72}, {280, 178}, {150, 293}, {16, 181}});
  TEST_ASSERT_TRUE(flow.phase() == CalibrationPhase::Verifying);
  TouchCalibration fitted = flow.active();
  flow.keep();
  TEST_ASSERT_TRUE(flow.phase() == CalibrationPhase::Idle);
  TEST_ASSERT_TRUE(flow.takeOutcome() == CalibrationOutcome::Saved);

  TouchCalibrationFlow reloaded(blobs);
  reloaded.begin();
  TEST_ASSERT_TRUE(reloaded.active() == fitted);
}

void test_calibration_flow_reverts_unconfirmed_fit() {
  FakeBlobStore blobs;
  TouchCalibrationFlow flow(blobs);
  flow.begin();
  flow.start(0);
  // A plausible but different mapping.
  uint32_t now = feedTaps(flow, {{170, 60}, {300, 170}, {170, 280}, {40, 170}});
  TEST_ASSERT_TRUE(flow.phase() == CalibrationPhase::Verifying);
  TEST_ASSERT_FALSE(flow.active() == TouchCalibration::defaults());
  uint32_t deadline = now + flow.verifyRemainingMs(now);
  flow.tick(deadline - 1);
  TEST_ASSERT_TRUE(flow.phase() == CalibrationPhase::Verifying);
  flow.tick(deadline);
  TEST_ASSERT_TRUE(flow.active() == TouchCalibration::defaults());
  TEST_ASSERT_TRUE(flow.takeOutcome() == CalibrationOutcome::Reverted);
  TEST_ASSERT_TRUE(blobs.data.empty());
}

void test_calibration_flow_restarts_capture_after_rejected_fit() {
  FakeBlobStore blobs;
  TouchCalibrationFlow flow(blobs);
  flow.begin();
  flow.start(0);
  feedTaps(flow, {{180, 180}, {180, 180}, {180, 180}, {180, 180}});
  TEST_ASSERT_TRUE(flow.isCapturing());
  TEST_ASSERT_TRUE(flow.lastFitRejected());
  TEST_ASSERT_EQUAL_UINT(0, flow.targetsDone());
}

void test_encoder_moves_the_paddle_on_tableTennis_not_the_list() {
  // The game reads the knob directly (ADR 0022); nothing about a list
  // highlight makes sense while a ball is in play.
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Games, {}});
  tabs.activeStack().push(Screen{ScreenKind::TableTennis, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onEncoderDelta(-3, 0);

  TEST_ASSERT_EQUAL_INT(1, sink.paddleCalls);
  TEST_ASSERT_EQUAL_INT16(-3, sink.lastPaddleDelta);
  TEST_ASSERT_EQUAL_INT(0, sink.calls);
}

void test_the_games_list_still_moves_the_highlight() {
  // Only the game itself takes the knob over -- the list in front of it
  // is an ordinary list.
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Games, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onEncoderDelta(2, 0);

  TEST_ASSERT_EQUAL_INT(1, sink.calls);
  TEST_ASSERT_EQUAL_INT(0, sink.paddleCalls);
}

void test_encoder_cancels_touch_calibration() {
  FakeDriver driver;
  FakeStore store;
  VolumePersistence volume(store);
  PlaybackStateMachine playback(driver, volume);
  TabController tabs;
  tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
  tabs.activeStack().push(Screen{ScreenKind::TouchCalibration, {}});
  RecordingListSink sink;
  BrightnessSetting brightness(store);
  Shuttle shuttle(playback);
  FakeBlobStore blobs;
  TouchCalibrationFlow calibration(blobs);
  calibration.begin();
  calibration.start(0);
  feedTaps(calibration, {{170, 60}, {300, 170}, {170, 280}, {40, 170}});
  SleepTimer sleepTimer;
  InputRouter router(tabs, playback, shuttle, brightness, sleepTimer,
                     calibration, sink);

  router.onEncoderDelta(1, 0);

  TEST_ASSERT_TRUE(calibration.phase() == CalibrationPhase::Idle);
  TEST_ASSERT_TRUE(calibration.active() == TouchCalibration::defaults());
  TEST_ASSERT_TRUE(tabs.activeStack().current().kind == ScreenKind::Settings);
  TEST_ASSERT_EQUAL_INT(0, sink.calls);
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

void test_gesture_carries_where_the_finger_went_down() {
  GestureRecognizer recognizer;
  recognizer.feed(TouchSample{100, 60, true});
  recognizer.feed(TouchSample{160, 70, true});
  auto event = recognizer.feed(TouchSample{160, 70, false});

  // The screen owner decides what a swipe means from where it began --
  // one that started on a control was a tap aimed at that control.
  TEST_ASSERT_TRUE(event.has_value());
  TEST_ASSERT_TRUE(event->type == GestureType::SwipeLeftToRight);
  TEST_ASSERT_EQUAL_INT16(100, event->startX);
  TEST_ASSERT_EQUAL_INT16(60, event->startY);
  TEST_ASSERT_EQUAL_INT16(160, event->x);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_encoder_scrolls_list_on_browse_screen);
  RUN_TEST(test_encoder_adjusts_volume_on_now_playing_screen);
  RUN_TEST(test_encoder_shuttles_instead_of_volume_while_held);
  RUN_TEST(test_encoder_adjusts_brightness_on_brightness_screen);
  RUN_TEST(test_encoder_sets_sleep_timer_on_sleep_screen);
  RUN_TEST(test_encoder_moves_tile_selection_on_home);
  RUN_TEST(test_swipe_pops_when_possible);
  RUN_TEST(test_gesture_carries_where_the_finger_went_down);
  RUN_TEST(test_swipe_switches_tab_at_root);
  RUN_TEST(test_tap_is_not_routed_by_input_router);
  RUN_TEST(test_gesture_recognizer_detects_tap);
  RUN_TEST(test_gesture_recognizer_detects_swipe_left_to_right);
  RUN_TEST(test_gesture_recognizer_ignores_right_to_left_swipe);
  RUN_TEST(test_gesture_recognizer_ignores_vertical_drag);
  RUN_TEST(test_touch_calibration_maps_measured_raw_x_back_to_visual_x);
  RUN_TEST(test_touch_calibration_clamps_to_screen);
  RUN_TEST(test_touch_calibration_blob_round_trips);
  RUN_TEST(test_touch_calibration_rejects_corrupt_blob);
  RUN_TEST(test_calibrator_fits_measured_skew);
  RUN_TEST(test_calibrator_rejects_taps_on_one_spot);
  RUN_TEST(test_calibrator_rejects_swapped_targets);
  RUN_TEST(test_calibrator_ignores_press_right_after_start);
  RUN_TEST(test_calibration_flow_keeps_confirmed_fit_and_persists_it);
  RUN_TEST(test_calibration_flow_reverts_unconfirmed_fit);
  RUN_TEST(test_calibration_flow_restarts_capture_after_rejected_fit);
  RUN_TEST(test_encoder_cancels_touch_calibration);
  RUN_TEST(test_encoder_moves_the_paddle_on_tableTennis_not_the_list);
  RUN_TEST(test_the_games_list_still_moves_the_highlight);
  RUN_TEST(test_touch_latch_reports_press_that_ended_between_reads);
  RUN_TEST(test_touch_latch_passes_through_held_press);
  return UNITY_END();
}
