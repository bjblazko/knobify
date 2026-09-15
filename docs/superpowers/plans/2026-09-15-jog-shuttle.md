# Jog/Shuttle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While you hold the time pill on Now Playing, the knob shuttles through the track (±5 steps, 2×–32×, CD-style cue). An ink arc inside the progress ring shows the speed.

**Architecture:**

- **`playback::Shuttle`** (pure logic, host-tested) owns hold/step/cue-cycle/end-stop behavior. It drives `PlaybackStateMachine`, which gains `seekBy()` and `canSeek()`. `PlaybackDriver` gains `seekByMs()`, implemented with ESP32-audioI2S's `setFilePos`.
- **`InputRouter`** sends encoder deltas to `Shuttle` instead of volume while it's held.
- **`ScreenManager`** turns the time readout into a hold button and draws the marker and the symmetrical shuttle arc.

**Tech Stack:** C++17, PlatformIO (`native` Unity tests, `esp32-s3` firmware), LVGL 8.3, ESP32-audioI2S 2.3.0.

**Spec:** `docs/adr/0013-jog-shuttle.md`

## Global Constraints

**Code and tests**
- Logic lives in host-testable headers under `lib/`. Callers pass timestamps (`nowMs`); logic never reads a clock itself (ADR 0003).
- Every call into `Audio` in `Esp32AudioI2SDriver` takes `mutex_` via `MutexGuard` (ADR 0006).
- Namespace `knobify::…`. Match the surrounding comment style: explain *why*, and cite ADRs and the dates of hardware findings.
- Host tests: `pio test -e native` (a single suite: `pio test -e native -f <suite>`).
- Firmware build: `pio run -e esp32-s3`.
- Flash only via the `flash-device` skill.

**Behavior (from ADR 0013)**
- Steps are ±5, speed `1 << |step|`, one detent = one step, clamped at ±5.
- Cue cycle `kCycleMs = 300`. Forward jump `(N − 1) · 300`, rewind jump `−(N + 1) · 300`.
- End margin `kEndMarginMs = 1000`. Parking at an end stop is a real pause (state `Paused`); leaving the stop resumes playback.
- Shuttling works only for `.mp3` and `.wav` (case-insensitive). `.ogg` gets no marks and no hold.
- On release the position stays where it is, and play/pause goes back to the state from before the hold.
- The hold also ends when you leave Now Playing, lock, the display turns off, the touch goes up, or the track changes.

**Display**
- LVGL's built-in Montserrat is ASCII-only: write the speed as `8x`, not `8×`.
- The round display: everything on Now Playing is horizontally centered, nothing sits in a corner.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `lib/playback/PlaybackDriver.h` | modify | + `seekByMs(int32_t)` |
| `lib/drivers-audio/Esp32AudioI2SDriver.h` | modify | implement `seekByMs` via bitrate + `setFilePos` |
| `lib/playback/PlaybackStateMachine.h` | modify | + `seekBy()`, `canSeek()` |
| `lib/playback/Shuttle.h` | create | hold/step/cue cycle/end stops/release |
| `lib/input/InputRouter.h` | modify | route encoder to `Shuttle` while held |
| `lib/ui-widgets/EdgeArc.h` | modify | + `mode` in `EdgeArcConfig` |
| `lib/ui/ScreenHelpers.h` | modify | `makeEdgeArcHost(parent, insetPx)` |
| `lib/ui/ScreenManager.h/.cpp` | modify | time pill, marker, shuttle arc, readout text |
| `src/main.cpp` | modify | wire `Shuttle`, safety releases, `tick()` |
| `test/test_playback/test_playback_state_machine.cpp` | modify | `seekBy`/`canSeek` tests, fake gains `seekByMs` |
| `test/test_shuttle/test_shuttle.cpp` | create | Shuttle tests |
| `test/test_input/test_input.cpp` | modify | routing test, fake + ctor update |
| `test/test_resume/test_resume.cpp` | modify | fake gains `seekByMs` |
| `README.md`, `docs/design/ux-guidelines.md`, `docs/adr/0013-jog-shuttle.md` | modify | docs |

---

### Task 1: Seeking in driver and state machine

**Files:**
- Modify: `lib/playback/PlaybackDriver.h`
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.h`
- Modify: `lib/playback/PlaybackStateMachine.h`
- Test: `test/test_playback/test_playback_state_machine.cpp`
- Modify (fakes only): `test/test_input/test_input.cpp`, `test/test_resume/test_resume.cpp`

**Interfaces:**
- Produces:
  - `virtual bool PlaybackDriver::seekByMs(int32_t deltaMs) = 0;`
  - `void PlaybackStateMachine::seekBy(int32_t deltaMs, uint32_t nowMs);`
  - `bool PlaybackStateMachine::canSeek() const;`

- [ ] **Step 1: Add `seekByMs` to the interface and every fake so the suites still compile**

In `lib/playback/PlaybackDriver.h`, after `filePosition()`:

```cpp
  // Jumps the decoder by `deltaMs` within the current file (negative =
  // back), for jog/shuttle (ADR 0013). Approximate: converted to bytes via
  // the average bitrate. False if nothing seekable is loaded.
  virtual bool seekByMs(int32_t deltaMs) = 0;
```

In `test/test_input/test_input.cpp` and `test/test_resume/test_resume.cpp`, add to `FakeDriver`:

```cpp
  bool seekByMs(int32_t) override { return true; }
```

In `test/test_playback/test_playback_state_machine.cpp`, add to `FakeDriver` (the `#include <vector>` is already there):

```cpp
  bool seekByMs(int32_t deltaMs) override {
    seeks.push_back(deltaMs);
    return seekSucceeds;
  }
  std::vector<int32_t> seeks;
  bool seekSucceeds = true;
```

- [ ] **Step 2: Write the failing state-machine tests**

Append before `main` in `test/test_playback/test_playback_state_machine.cpp`:

```cpp
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

void test_can_seek_only_mp3_and_wav_with_a_track() {
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
  sm.play({"/a.ogg"}, 0, 0);
  TEST_ASSERT_FALSE(sm.canSeek());
  sm.play({"/noextension"}, 0, 0);
  TEST_ASSERT_FALSE(sm.canSeek());
}
```

Register them in `main`:

```cpp
  RUN_TEST(test_seek_by_moves_elapsed_time_and_driver);
  RUN_TEST(test_seek_by_clamps_at_track_start);
  RUN_TEST(test_seek_by_keeps_elapsed_when_driver_refuses);
  RUN_TEST(test_seek_by_ignored_when_stopped);
  RUN_TEST(test_can_seek_only_mp3_and_wav_with_a_track);
```

- [ ] **Step 3: Run the tests and confirm they fail**

Run: `pio test -e native -f test_playback`
Expected: compile error, `'class PlaybackStateMachine' has no member named 'seekBy'`.

- [ ] **Step 4: Implement `seekBy` and `canSeek`**

In `lib/playback/PlaybackStateMachine.h`, add `#include <cctype>` to the includes. After `onTrackFinished`, add:

```cpp
  // Moves within the current track (jog/shuttle, ADR 0013) and shifts the
  // wall-clock elapsed time by the same amount, so the readout and ring
  // follow. Clamped at the track start; the end is the caller's job (it
  // knows its safety margin). Nothing loaded (stopped, or cued after a
  // reboot) -> no-op.
  void seekBy(int32_t deltaMs, uint32_t nowMs) {
    if (state_ == PlaybackState::Stopped || cued_) return;
    int64_t elapsed = elapsedMs(nowMs);
    if (elapsed + deltaMs < 0) deltaMs = static_cast<int32_t>(-elapsed);
    if (deltaMs == 0) return;
    if (!driver_.seekByMs(deltaMs)) return;
    // Modular arithmetic: subtracting a negative delta moves the start
    // later, i.e. less elapsed.
    trackStartMs_ -= static_cast<uint32_t>(deltaMs);
  }

  // Whether the current track can be shuttled: ESP32-audioI2S only seeks
  // within MP3 and WAV of the formats knobify plays (not Ogg). By
  // extension, so it's known before a cued track is loaded.
  bool canSeek() const {
    if (state_ == PlaybackState::Stopped || queue_.empty()) return false;
    const std::string &path = queue_.current();
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "mp3" || ext == "wav";
  }
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `pio test -e native -f test_playback`
Expected: all tests PASS.

- [ ] **Step 6: Implement the real driver**

In `lib/drivers-audio/Esp32AudioI2SDriver.h`, after `filePosition()`:

```cpp
  // setTimeOffset() only takes whole seconds -- too coarse for 2× cue
  // (ADR 0013) -- so convert ms to bytes with the average bitrate and use
  // setFilePos(), which the decoder applies on its next chunk (and
  // re-aligns to a frame boundary itself). getFilePos() includes the
  // library's read-ahead, so jumps are approximate; fine for cueing.
  bool seekByMs(int32_t deltaMs) override {
    MutexGuard guard(mutex_);
    uint32_t avgBitrate = audio_.getBitRate(true);
    if (avgBitrate == 0) return false;
    int64_t bytes = static_cast<int64_t>(deltaMs) * avgBitrate / 8000;
    int64_t target = static_cast<int64_t>(audio_.getFilePos()) + bytes;
    int64_t start = audio_.getAudioDataStartPos();
    int64_t end = audio_.getFileSize();
    if (target < start) target = start;
    if (target > end) target = end;
    return audio_.setFilePos(static_cast<uint32_t>(target));
  }
```

- [ ] **Step 7: Run all host tests and the firmware build**

Run: `pio test -e native && pio run -e esp32-s3`
Expected: all suites PASS, firmware builds with `SUCCESS`.

- [ ] **Step 8: Commit**

```bash
git add lib/playback/PlaybackDriver.h lib/playback/PlaybackStateMachine.h lib/drivers-audio/Esp32AudioI2SDriver.h test/test_playback test/test_input test/test_resume
git commit -m "Seek within a track by milliseconds

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1"
```

---

### Task 2: `Shuttle` logic

**Files:**
- Create: `lib/playback/Shuttle.h`
- Test: `test/test_shuttle/test_shuttle.cpp`

**Interfaces:**
- Consumes (Task 1):
  - `PlaybackStateMachine::seekBy(int32_t, uint32_t)`
  - `canSeek()`
  - `togglePlayPause(uint32_t)`
  - `state()`
  - `elapsedMs(uint32_t)`
  - `durationSeconds()`
  - `currentPath()`
- Produces:
  - `class knobify::playback::Shuttle`, constructed as `explicit Shuttle(PlaybackStateMachine &)`
  - `static constexpr int8_t kMaxStep = 5;`
  - `static constexpr uint32_t kCycleMs = 300;`
  - `static constexpr uint32_t kEndMarginMs = 1000;`
  - `bool hold(uint32_t nowMs);` returns false when the track can't shuttle
  - `void release(uint32_t nowMs);`
  - `void turn(int delta, uint32_t nowMs);`
  - `void tick(uint32_t nowMs);`
  - `bool isHeld() const;`
  - `int8_t step() const;`

- [ ] **Step 1: Write the failing tests**

Create `test/test_shuttle/test_shuttle.cpp`:

```cpp
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
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `pio test -e native -f test_shuttle`
Expected: compile error, `Shuttle.h: No such file or directory`.

- [ ] **Step 3: Implement `Shuttle`**

Create `lib/playback/Shuttle.h`:

```cpp
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>

#include "PlaybackStateMachine.h"

namespace knobify::playback {

// Jog/shuttle on Now Playing (ADR 0013): while the time pill is held, knob
// detents set a speed step (±kMaxStep, speed 1 << |step|) and the track is
// cued CD-style -- a kCycleMs snippet at normal speed, then a jump sized so
// the overall rate matches the step. Pure logic over PlaybackStateMachine,
// host-testable with explicit timestamps like everything in lib/playback.
//
// End stops are real pauses ("parked"): the state machine then reports
// Paused, so elapsed time freezes and main.cpp's track-finished detection
// (which only runs while Playing) can't fire and skip to the next track.
class Shuttle {
 public:
  static constexpr int8_t kMaxStep = 5;
  static constexpr uint32_t kCycleMs = 300;
  // Forward parks this far before the end, so the track can't finish
  // while held.
  static constexpr uint32_t kEndMarginMs = 1000;

  explicit Shuttle(PlaybackStateMachine &playback) : playback_(playback) {}

  // Starts a hold; false (and nothing held) for a track that can't seek.
  // Cueing is audible even from pause, so a paused track plays for the
  // hold's duration.
  bool hold(uint32_t nowMs) {
    if (held_) return true;
    if (!playback_.canSeek()) return false;
    held_ = true;
    step_ = 0;
    parked_ = false;
    path_ = playback_.currentPath();
    wasPaused_ = playback_.state() == PlaybackState::Paused;
    if (wasPaused_) playback_.togglePlayPause(nowMs);
    cycleStartMs_ = nowMs;
    return true;
  }

  // Ends a hold: the position stays, play/pause returns to what it was.
  void release(uint32_t nowMs) {
    if (!held_) return;
    bool playing = playback_.state() == PlaybackState::Playing;
    if (wasPaused_ == playing) playback_.togglePlayPause(nowMs);
    drop();
  }

  // `delta` in detents; positive = forward.
  void turn(int delta, uint32_t) {
    if (!held_) return;
    int step = step_ + delta;
    if (step > kMaxStep) step = kMaxStep;
    if (step < -kMaxStep) step = -kMaxStep;
    step_ = static_cast<int8_t>(step);
  }

  // Call every loop() iteration.
  void tick(uint32_t nowMs) {
    if (!held_) return;
    // Skipped or finished into another track: that's the buttons' job,
    // and the old hold means nothing for the new track.
    if (playback_.state() == PlaybackState::Stopped ||
        playback_.currentPath() != path_) {
      drop();
      return;
    }
    if (parked_) {
      bool stillPushing = parkedAtEnd_ ? step_ > 0 : step_ < 0;
      if (stillPushing) return;
      parked_ = false;
      playback_.togglePlayPause(nowMs);
      cycleStartMs_ = nowMs;
      return;
    }
    if (step_ == 0) {
      cycleStartMs_ = nowMs;
      return;
    }
    if (nowMs - cycleStartMs_ < kCycleMs) return;
    cycleStartMs_ = nowMs;

    uint32_t durationMs = playback_.durationSeconds() * 1000u;
    if (durationMs <= kEndMarginMs) return;  // Unknown (not parsed yet) or tiny.
    int64_t speed = int64_t{1} << std::abs(step_);
    int64_t jump = step_ > 0 ? (speed - 1) * kCycleMs : -(speed + 1) * kCycleMs;
    int64_t position = playback_.elapsedMs(nowMs);
    int64_t endStop = durationMs - kEndMarginMs;
    int64_t target = position + jump;
    bool park = false;
    if (target >= endStop) {
      target = endStop;
      park = true;
      parkedAtEnd_ = true;
    } else if (target <= 0) {
      target = 0;
      park = true;
      parkedAtEnd_ = false;
    }
    playback_.seekBy(static_cast<int32_t>(target - position), nowMs);
    if (park) {
      parked_ = true;
      if (playback_.state() == PlaybackState::Playing) {
        playback_.togglePlayPause(nowMs);
      }
    }
  }

  bool isHeld() const { return held_; }
  int8_t step() const { return step_; }

 private:
  void drop() {
    held_ = false;
    step_ = 0;
    parked_ = false;
  }

  PlaybackStateMachine &playback_;
  bool held_ = false;
  int8_t step_ = 0;
  bool wasPaused_ = false;
  bool parked_ = false;
  bool parkedAtEnd_ = false;
  uint32_t cycleStartMs_ = 0;
  std::string path_;
};

}  // namespace knobify::playback
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `pio test -e native -f test_shuttle`
Expected: 13 tests PASS.

What to check in a failing case:
- `test_track_change_drops_the_hold`: `next()` runs at 100, and `tick(400)` must drop the hold *before* any jump.
- `test_rewind_parks_at_track_start`: `seekBy` clamps at 0 on its own, and `Shuttle` sets `target = 0`, so the delta is `-1300`.

- [ ] **Step 5: Commit**

```bash
git add lib/playback/Shuttle.h test/test_shuttle
git commit -m "Add shuttle logic: speed steps, CD-style cue and end stops

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1"
```

---

### Task 3: Route the knob to the shuttle while held

**Files:**
- Modify: `lib/input/InputRouter.h`
- Test: `test/test_input/test_input.cpp`

**Interfaces:**
- Consumes (Task 2): `Shuttle::isHeld()`, `Shuttle::turn(int, uint32_t)`, `Shuttle::hold(uint32_t)`
- Produces:
  - `InputRouter(navigation::TabController &, playback::PlaybackStateMachine &, playback::Shuttle &, power::BrightnessSetting &, ListMoveSink &)`, a new third parameter

- [ ] **Step 1: Update every existing construction, then write the failing test**

In `test/test_input/test_input.cpp`:
- Add `#include "Shuttle.h"` and `using knobify::playback::Shuttle;`.
- Replace every `InputRouter router(tabs, playback, brightness, sink);` with the two lines below:

```cpp
  Shuttle shuttle(playback);
  InputRouter router(tabs, playback, shuttle, brightness, sink);
```

Check with: `grep -n 'InputRouter router(' test/test_input/test_input.cpp` (every hit must include `shuttle`).

The `FakeDriver` here reports `durationSeconds() == 0` and `playFile` succeeds, which is enough. Add the test:

```cpp
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
  InputRouter router(tabs, playback, shuttle, brightness, sink);

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
```

Register `RUN_TEST(test_encoder_shuttles_instead_of_volume_while_held);` after `test_encoder_adjusts_volume_on_now_playing_screen`.

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `pio test -e native -f test_input`
Expected: compile error, no matching constructor for `InputRouter` with 5 arguments.

- [ ] **Step 3: Implement the routing**

In `lib/input/InputRouter.h`:
- Add `#include "Shuttle.h"`.
- Extend the constructor and the members.
- Change the Now Playing case.

```cpp
  InputRouter(navigation::TabController &tabs,
              playback::PlaybackStateMachine &playback,
              playback::Shuttle &shuttle,
              power::BrightnessSetting &brightness, ListMoveSink &listSink)
      : tabs_(tabs),
        playback_(playback),
        shuttle_(shuttle),
        brightness_(brightness),
        listSink_(listSink) {}
```

```cpp
      case navigation::ScreenKind::NowPlaying:
        // Holding the time pill turns the knob into a shuttle (ADR 0013);
        // the volume never changes during a hold.
        if (shuttle_.isHeld()) {
          shuttle_.turn(delta, nowMs);
        } else {
          playback_.adjustVolume(delta, nowMs);
        }
        break;
```

```cpp
  playback::Shuttle &shuttle_;
```

(Declare `shuttle_` between `playback_` and `brightness_`, matching initializer order.) Also extend the class comment's first sentence: "…routes encoder deltas to list/tile selection, volume, the shuttle (while held, ADR 0013) or brightness…".

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `pio test -e native`
Expected: all suites PASS. The firmware isn't built yet because `main.cpp` still uses the old constructor. Task 4 fixes that.

- [ ] **Step 5: Commit**

```bash
git add lib/input/InputRouter.h test/test_input/test_input.cpp
git commit -m "Route the knob to the shuttle while it is held

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1"
```

---

### Task 4: Time pill, marker, shuttle arc and main loop wiring

UI and wiring have no host tests (LVGL/hardware, per ADR 0003). You verify them on the device.

**Files:**
- Modify: `lib/ui-widgets/EdgeArc.h`
- Modify: `lib/ui/ScreenHelpers.h`
- Modify: `lib/ui/ScreenManager.h`
- Modify: `lib/ui/ScreenManager.cpp` (render reset ~line 94–108, `renderNowPlaying()` ~559–679, `updateElapsedTimeDisplay()` ~807)
- Modify: `src/main.cpp` (globals ~190–210, `loop()` encoder branch ~492–506, after `tickVolumeHud`)

**Interfaces:**
- Consumes:
  - `Shuttle::hold`, `release`, `tick`, `isHeld`, `step` (Task 2)
  - `PlaybackStateMachine::canSeek()` (Task 1)
  - the new `InputRouter` constructor (Task 3)
- Produces:
  - `EdgeArcConfig::mode` (`lv_arc_mode_t`, default `LV_ARC_MODE_NORMAL`)
  - `makeEdgeArcHost(lv_obj_t *parent, lv_coord_t insetPx = 0)`
  - `ScreenManager` constructor gains `playback::Shuttle &shuttle` right after `playback`

- [ ] **Step 1: `EdgeArc` mode**

In `lib/ui-widgets/EdgeArc.h`, add this to `EdgeArcConfig` after `hasBackgroundColor`:

```cpp
  // LV_ARC_MODE_SYMMETRICAL fills from the middle of the range outwards --
  // the shuttle arc growing either way from the top (ADR 0013).
  lv_arc_mode_t mode = LV_ARC_MODE_NORMAL;
```

In `create()`, directly before `lv_arc_set_range(arc_, min, max);`:

```cpp
    lv_arc_set_mode(arc_, config.mode);
```

- [ ] **Step 2: Inset arc host**

In `lib/ui/ScreenHelpers.h`, replace `makeEdgeArcHost`:

```cpp
// `insetPx` shrinks the host on every side, for a ring drawn just inside
// another one (the shuttle arc inside the progress ring, ADR 0013).
inline lv_obj_t *makeEdgeArcHost(lv_obj_t *parent, lv_coord_t insetPx = 0) {
  lv_obj_t *host = lv_obj_create(parent);
  lv_obj_set_size(host, drivers::kLcdHorRes + 40 - 2 * insetPx,
                  drivers::kLcdVerRes + 40 - 2 * insetPx);
  lv_obj_center(host);
  lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(host, 0, 0);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_CLICKABLE);
  return host;
}
```

(Keep any existing comment above the function and add the `insetPx` sentence to it.)

- [ ] **Step 3: `ScreenManager` header**

In `lib/ui/ScreenManager.h`:
- Add `#include "Shuttle.h"`.
- Add the constructor parameter `playback::Shuttle &shuttle,` after `playback::PlaybackStateMachine &playback,`, with the initializer `shuttle_(shuttle),` after `playback_(playback),`, and the member `playback::Shuttle &shuttle_;` after `playback_`.

In the private section, near `setProgressRingVisible`:

```cpp
  // Shows/hides the top marker and shuttle arc for shownShuttle* (ADR 0013).
  void applyShuttleIndicator();
  static void onTimePillPressed(lv_event_t *e);
  static void onTimePillReleased(lv_event_t *e);
```

Among the constants, after `kTransportCenterY`:

```cpp
  // Time pill (ADR 0013): between the shuffle/repeat toggles (x = ±100,
  // 44 px wide -> inner edges at ±78), leaving 12 px either side.
  static constexpr lv_coord_t kTimePillW = 132;
  static constexpr lv_coord_t kTimePillH = 28;
```

Among the members, after `progressArc_`:

```cpp
  // Null for tracks that can't shuttle -- then elapsedLabel_ is a plain label.
  lv_obj_t *timePill_ = nullptr;
  lv_obj_t *shuttleArcHost_ = nullptr;
  ui_widgets::EdgeArc shuttleArc_;
  lv_obj_t *shuttleMarker_ = nullptr;
  // What the pill text and arc currently show, to redraw only on change.
  bool shownShuttleHeld_ = false;
  int8_t shownShuttleStep_ = 0;
```

- [ ] **Step 4: Reset the new pointers on render**

In `ScreenManager.cpp`'s render reset block (after `progressArcHost_ = nullptr;`):

```cpp
  timePill_ = nullptr;
  shuttleArcHost_ = nullptr;
  shuttleMarker_ = nullptr;
  shownShuttleHeld_ = false;
  shownShuttleStep_ = 0;
```

- [ ] **Step 5: Marker and shuttle arc in `renderNowPlaying()`**

Directly after `lv_obj_add_flag(progressArcHost_, LV_OBJ_FLAG_HIDDEN);` (end of the progress ring block):

```cpp
  // Shuttle indicator (ADR 0013), only while the time pill is held: an ink
  // tick at the top marks normal speed, and a thin ink arc just inside the
  // progress ring grows from it -- clockwise forward, counterclockwise
  // back, 24° per step. The one deliberate exception to "one edge ring at
  // a time": speed and position are both needed while scrubbing. Ink, not
  // a signal color: it sits on the surface, not against the housing, and
  // yellow/orange/green already mean something.
  constexpr lv_coord_t kShuttleArcWidth = 5;
  constexpr lv_coord_t kShuttleArcGap = 3;
  ui_widgets::EdgeArcConfig shuttleArcConfig;
  shuttleArcConfig.startAngle = 150;
  shuttleArcConfig.endAngle = 30;
  shuttleArcConfig.widthPx = kShuttleArcWidth;
  shuttleArcConfig.color = theme::ink();
  shuttleArcConfig.mode = LV_ARC_MODE_SYMMETRICAL;
  shuttleArcHost_ = makeEdgeArcHost(screen_, progressArcConfig.widthPx + kShuttleArcGap);
  shuttleArc_.create(shuttleArcHost_, shuttleArcConfig, -playback::Shuttle::kMaxStep,
                     playback::Shuttle::kMaxStep);
  shuttleArc_.setValue(static_cast<int32_t>(0));
  // No track behind the arc: only the speed is drawn.
  lv_obj_set_style_arc_opa(shuttleArc_.raw(), LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);

  // Same host as the progress ring, so it lines up with it wherever that
  // ring actually lands on the bezel.
  shuttleMarker_ = lv_obj_create(progressArcHost_);
  lv_obj_set_size(shuttleMarker_, 4,
                  progressArcConfig.widthPx + kShuttleArcGap + kShuttleArcWidth);
  lv_obj_align(shuttleMarker_, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(shuttleMarker_, theme::ink(), 0);
  lv_obj_set_style_border_width(shuttleMarker_, 0, 0);
  lv_obj_set_style_radius(shuttleMarker_, 0, 0);
  lv_obj_set_style_pad_all(shuttleMarker_, 0, 0);
  lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);
```

`shuttleArc_.setValue(static_cast<int32_t>(0))` picks the `int32_t` overload; the `float` overload would map 0.0 to −5.

- [ ] **Step 6: The time pill replaces the plain label for seekable tracks**

Replace the elapsed-label block (from `elapsedLabel_ = lv_label_create(screen_);` through `lv_label_set_text(elapsedLabel_, "0:00");`) with:

```cpp
  const lv_coord_t timeY = kTransportCenterY + 44;
  if (playback_.canSeek()) {
    // Hold-and-turn shuttle (ADR 0013): the readout of the song position
    // is the control that changes it. ◀◀ ▶▶ marks say it can be held;
    // no marks (Ogg) means it can't. No extended hit area -- the
    // shuffle/repeat toggles sit 12 px away.
    timePill_ = makeHoldButton(screen_, "0:00", kTimePillW, kTimePillH,
                               LV_ALIGN_TOP_MID, 0, timeY - 6,
                               &ScreenManager::onTimePillPressed,
                               &ScreenManager::onTimePillReleased, this,
                               ButtonRole::Secondary, &lv_font_montserrat_14);
    lv_obj_set_ext_click_area(timePill_, 0);
    elapsedLabel_ = lv_obj_get_child(timePill_, 0);
  } else {
    elapsedLabel_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(elapsedLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(elapsedLabel_, theme::structure(), 0);
    lv_obj_align(elapsedLabel_, LV_ALIGN_TOP_MID, 0, timeY);
    lv_label_set_text(elapsedLabel_, "0:00");
  }
```

Keep the existing comment above the block. The following `updateElapsedTimeDisplay();` call stays as it is.

- [ ] **Step 7: Pill callbacks and indicator**

Add to `ScreenManager.cpp` after `setProgressRingVisible()`:

```cpp
void ScreenManager::onTimePillPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->shuttle_.hold(millis());
}

void ScreenManager::onTimePillReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->shuttle_.release(millis());
}

void ScreenManager::applyShuttleIndicator() {
  if (!shuttleArcHost_ || !shuttleMarker_) return;
  if (shownShuttleHeld_) {
    shuttleArc_.setValue(static_cast<int32_t>(shownShuttleStep_));
    lv_obj_clear_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);
    // A volume HUD still fading out from before the hold would hide the
    // progress ring; the knob isn't setting volume now, so end it.
    if (volumeHudVisible_) volumeHudHideAtMs_ = 0;
  } else {
    lv_obj_add_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);
  }
}
```

- [ ] **Step 8: Readout text follows the shuttle**

Replace `updateElapsedTimeDisplay()` with:

```cpp
void ScreenManager::updateElapsedTimeDisplay() {
  if (!elapsedLabel_) return;
  uint32_t elapsedMs = playback_.elapsedMs(millis());
  uint32_t totalSeconds = elapsedMs / 1000;
  bool held = shuttle_.isHeld();
  int8_t step = shuttle_.step();
  bool shuttleChanged = held != shownShuttleHeld_ || step != shownShuttleStep_;
  if (shuttleChanged) {
    shownShuttleHeld_ = held;
    shownShuttleStep_ = step;
    applyShuttleIndicator();
  }
  if (static_cast<int32_t>(totalSeconds) != lastShownSecond_ || shuttleChanged) {
    lastShownSecond_ = static_cast<int32_t>(totalSeconds);
    durationSeconds_ = playback_.durationSeconds();
    unsigned em = static_cast<unsigned>(totalSeconds / 60);
    unsigned es = static_cast<unsigned>(totalSeconds % 60);
    unsigned dm = static_cast<unsigned>(durationSeconds_ / 60);
    unsigned ds = static_cast<unsigned>(durationSeconds_ % 60);
    char text[40];
    if (held && step != 0) {
      // Speed instead of the total, so the pill doesn't grow. ASCII "x":
      // the built-in font has no "×".
      snprintf(text, sizeof(text), "%u:%02u %s %ux", em, es,
               step > 0 ? LV_SYMBOL_FORWARD : LV_SYMBOL_BACKWARD,
               1u << std::abs(step));
    } else if (timePill_ && durationSeconds_ != 0) {
      snprintf(text, sizeof(text), LV_SYMBOL_BACKWARD " %u:%02u / %u:%02u " LV_SYMBOL_FORWARD,
               em, es, dm, ds);
    } else if (timePill_) {
      snprintf(text, sizeof(text), LV_SYMBOL_BACKWARD " %u:%02u " LV_SYMBOL_FORWARD, em, es);
    } else if (durationSeconds_ != 0) {
      snprintf(text, sizeof(text), "%u:%02u / %u:%02u", em, es, dm, ds);
    } else {
      snprintf(text, sizeof(text), "%u:%02u", em, es);
    }
    lv_label_set_text(elapsedLabel_, text);
    // Honest: no duration known -> no progress ring at all, rather than
    // a ring that never moves.
    if (!volumeHudVisible_) setProgressRingVisible(durationSeconds_ != 0);
  }
  if (durationSeconds_ != 0) {
    progressArc_.setValue(static_cast<float>(elapsedMs) /
                          (static_cast<float>(durationSeconds_) * 1000.0f));
  }
}
```

Make sure `<cstdlib>` is included in `ScreenManager.cpp` for `std::abs`; add it if it's missing.

- [ ] **Step 9: Wire it up in `main.cpp`**

Globals: add `#include "Shuttle.h"` next to the other `lib/playback` includes. After `g_playback`:

```cpp
knobify::playback::Shuttle g_shuttle(g_playback);
```

Pass `g_shuttle` to `ScreenManager` right after `g_playback`, and to `InputRouter`:

```cpp
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_shuttle,
                                          g_brightness, g_screenManager);
```

In `loop()`'s unlocked encoder branch, keep the HUD out of a hold:

```cpp
      g_inputRouter.onEncoderDelta(encoderDelta, now);
      // Cheap (no full re-render) so it can run on every tick -- see
      // ScreenManager::updateVolumeDisplay(). A no-op on any screen
      // other than Now Playing, and skipped while the knob shuttles.
      if (!g_shuttle.isHeld()) g_screenManager.updateVolumeDisplay(now);
      g_screenManager.updateBrightnessDisplay();
```

Directly after `g_screenManager.tickVolumeHud(now);`:

```cpp
  // A shuttle hold ends with the finger (the pill's RELEASED/PRESS_LOST
  // normally does it; this also covers the pill being deleted by a
  // re-render mid-hold), or when its screen goes away (ADR 0013).
  if (g_shuttle.isHeld() &&
      (!touchSample.pressed || !displayOn || g_lockController.isLocked() ||
       g_tabs.activeStack().current().kind !=
           knobify::navigation::ScreenKind::NowPlaying)) {
    g_shuttle.release(now);
  }
  g_shuttle.tick(now);
```

- [ ] **Step 10: Build**

Run: `pio test -e native && pio run -e esp32-s3`
Expected: all tests PASS, firmware `SUCCESS`. Fix compile errors as needed: missing includes, the `ScreenManager` constructor argument order in `main.cpp`.

- [ ] **Step 11: Flash and verify on the device**

Flash with the `flash-device` skill. Then ask the user to check these on Now Playing:

1. **Pill:** a grey pill `◀◀ 0:12 / 3:45 ▶▶` sits between shuffle and repeat, not touching either. Check with a track over 10 minutes too. If the text overflows the pill, reduce the marks (report back instead of redesigning).
2. **Ogg:** an `.ogg` track shows the plain time label, and holding it does nothing.
3. **Hold without turning:** the marker appears at the top and there's no audible change. On release the marker disappears.
4. **Forward:** hold and turn clockwise 1…5 detents. You hear cue snippets getting faster, the yellow ring races, the black arc grows clockwise, and the pill shows `▶▶ 2x` … `32x`. The 6th detent changes nothing.
5. **Rewind:** counterclockwise mirrors it.
6. **Release:** playback continues at normal speed from the new spot, and the volume is unchanged.
7. **From pause:** snippets play during the hold and it's paused again after release.
8. **End stops:** shuttle forward to the end: it goes silent about 1 s before the end without skipping, then plays out and advances after release. Rewind to 0:00: silent, and it continues from the start after release.
9. **Knob after release:** it sets the volume again and the HUD appears.
10. **Hold ends on its own:** tap ⏭ with the other hand during a hold; the hold ends and the new track plays normally.

If the snippets sound choppy or the jumps land far off, record what you heard. Tuning `kCycleMs` is a follow-up that needs the user's feedback, so don't guess.

- [ ] **Step 12: Commit**

```bash
git add lib/ui-widgets/EdgeArc.h lib/ui/ScreenHelpers.h lib/ui/ScreenManager.h lib/ui/ScreenManager.cpp src/main.cpp
git commit -m "Shuttle through a track by holding the time and turning the knob

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1"
```

---

### Task 5: Documentation

**Files:**
- Modify: `README.md`
- Modify: `docs/design/ux-guidelines.md`
- Modify: `docs/adr/0013-jog-shuttle.md`

**Interfaces:** none.

- [ ] **Step 1: README**

Remove the out-of-scope line `- Jog/shuttle-style scrubbing through a track's playback position`. Add to the Goal list, after the shuffle/repeat bullet:

```markdown
- Jog/shuttle: hold the time readout on Now Playing and turn the knob to
  fast forward or rewind (five speeds each way, CD-style cue); letting go
  plays on from there — see [ADR 0013](docs/adr/0013-jog-shuttle.md).
```

- [ ] **Step 2: UX guidelines**

In §5, the "Context-sensitive encoder" bullet: after "on Now Playing, rotating adjusts volume", add "(or shuttles through the track while the time pill is held, ADR 0013)".

In §6, extend the "One edge ring visible at a time" bullet with:

```markdown
  One exception (ADR 0013): while the time pill is held, a thin `ink`
  shuttle arc sits just inside the progress ring with a marker at the top
  — speed and position are both needed while scrubbing, and both vanish
  with the finger.
```

- [ ] **Step 3: Make the ADR match what was built**

In `docs/adr/0013-jog-shuttle.md`:
- In "Hold the time pill and turn", change `` `1:23 ▶▶ 8×` or `1:23 ◀◀ 8×` `` to `` `1:23 ▶▶ 8x` or `1:23 ◀◀ 8x` (ASCII `x`: the built-in font has no `×`) ``.
- Replace the "Implementation outline" list with:

```markdown
- `playback::Shuttle` (`lib/playback/Shuttle.h`, pure logic, host-tested):
  hold/release, step clamping, cue-cycle timing and jump math, end stops
  (a real pause, so track-finished detection can't fire), restoring
  play/pause.
- `PlaybackDriver::seekByMs(int32_t)` in `Esp32AudioI2SDriver` under the
  existing mutex (ADR 0006), via average bitrate and `setFilePos`.
- `PlaybackStateMachine::seekBy()` shifts the wall-clock elapsed time by
  each jump; `canSeek()` decides by file extension (MP3/WAV), so a track
  cued after a reboot already shows its marks.
- `InputRouter` sends encoder deltas to `Shuttle` instead of
  `adjustVolume` while it is held. The pill's press/release callbacks call
  the shuttle directly; `main.cpp` also releases it when the touch ends
  or Now Playing goes away.
- `EdgeArcConfig` gains a `mode` field for the symmetrical arc;
  `makeEdgeArcHost` an inset.
```

- [ ] **Step 4: Commit**

```bash
git add README.md docs/design/ux-guidelines.md docs/adr/0013-jog-shuttle.md
git commit -m "Document jog/shuttle

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1"
```
