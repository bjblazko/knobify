# Ogg Vorbis Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play Ogg Vorbis files with the same behaviour as MP3/M4A — exact duration, jog/shuttle seeking, resume, spectrum, identical volume.

**Architecture:** ESP32-audioI2S keeps MP3/M4A/WAV/FLAC. A second decoder (stb_vorbis) runs on its own task and writes to the same I2S port through a shared `AudioOutputStage`, so volume, the sleep-timer gain and the spectrum ring behave identically on both paths. `Esp32AudioI2SDriver` dispatches by file extension and guarantees only one decoder runs at a time.

**Tech Stack:** C++17, Arduino-ESP32 2.0.x, FreeRTOS, ESP32-audioI2S 2.3.0, stb_vorbis (vendored), PlatformIO (`native` host tests with Unity, `esp32-s3` device build).

**Spec:** `docs/superpowers/specs/2026-09-16-vorbis-playback-design.md`

## Global Constraints

- Hardware/logic separation (`docs/coding-guidelines.md`): anything host-tested lives in `lib/playback` or `lib/library`; `lib/drivers-audio` may include Arduino/ESP-IDF headers. A unit test must never need a board.
- Module directories under `lib/` are flat — no subfolders (PlatformIO ignores them).
- Naming: `PascalCase` types, `camelCase` functions/variables, `SCREAMING_SNAKE_CASE` macros; explicit types over `auto` where the type isn't obvious.
- No dynamic allocation in the audio hot path; allocate buffers once when a track opens.
- Host tests: `pio test -e native`. Device build: `pio run -e esp32-s3`. Both must pass before every commit.
- **Do not flash the device without the user's go-ahead** — they use it while copying music (they asked explicitly, 2026-09-16).
- Never print to `Serial` while `UsbMscStorage::exporting()` is true (Arduino-ESP32 2.0.x `USBCDC::write()` spins forever when the CDC endpoint can't drain).
- Volume semantics that must not change: the library path halves every sample (`Audio::playSample()`'s `>>1`) and the existing hook doubles it back; the Vorbis path never sees that halving and must apply the volume table itself.
- Commit messages: plain prose, no "feat:"/"fix:" prefixes (match the repo's history), ending with the two attribution lines used by every commit in this repo.

---

### Task 1: AudioGain — the volume maths, host-tested

Pure logic extracted from the driver so both decode paths compute identical loudness, and so the table can be unit-tested against the library's own.

**Files:**
- Create: `lib/playback/AudioGain.h`
- Create: `test/test_audio_backends/test_audio_backends.cpp`
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.cpp` (use it in the hook and in `readRecentSamples()`)

**Interfaces:**
- Consumes: nothing.
- Produces: `knobify::playback::AudioGain` with `static constexpr uint16_t kUnityOutputGain = 4096;`, `static constexpr uint8_t kMaxVolumeStep = 21;`, `static int16_t applyOutputGain(int16_t sample, uint16_t outputGain);`, `static int16_t applyVolume(int16_t sample, uint8_t volumeStep, uint16_t outputGain);`, `static float linearGain(uint8_t volumeStep, uint16_t outputGain);`

- [ ] **Step 1: Write the failing tests**

Create `test/test_audio_backends/test_audio_backends.cpp`:

```cpp
#include <unity.h>

#include <cstdint>

#include "AudioGain.h"

using knobify::playback::AudioGain;

void setUp() {}
void tearDown() {}

void test_output_gain_scales_without_touching_volume() {
  // The library path: the sample already carries the volume, so only the
  // sleep-timer gain applies.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyOutputGain(1000, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT16(500, AudioGain::applyOutputGain(1000, AudioGain::kUnityOutputGain / 2));
  TEST_ASSERT_EQUAL_INT16(0, AudioGain::applyOutputGain(1000, 0));
  TEST_ASSERT_EQUAL_INT16(-500, AudioGain::applyOutputGain(-1000, AudioGain::kUnityOutputGain / 2));
}

void test_volume_step_21_is_unity_and_step_0_is_silence() {
  // The Vorbis path applies the volume itself.
  TEST_ASSERT_EQUAL_INT16(
      1000, AudioGain::applyVolume(1000, AudioGain::kMaxVolumeStep, AudioGain::kUnityOutputGain));
  TEST_ASSERT_EQUAL_INT16(0, AudioGain::applyVolume(1000, 0, AudioGain::kUnityOutputGain));
}

void test_volume_steps_are_monotonic_and_match_the_library_table() {
  // ESP32-audioI2S's volumetable (Audio.h), entry/64 per step.
  const uint8_t expected[22] = {0,  1,  2,  3,  4,  6,  8,  10, 12, 14, 17,
                                20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};
  for (uint8_t step = 0; step <= AudioGain::kMaxVolumeStep; ++step) {
    const float gain = AudioGain::linearGain(step, AudioGain::kUnityOutputGain);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, expected[step] / 64.0f, gain);
    if (step > 0) {
      TEST_ASSERT_TRUE(gain > AudioGain::linearGain(step - 1, AudioGain::kUnityOutputGain));
    }
  }
}

void test_values_are_clamped_and_steps_beyond_the_table_are_capped() {
  TEST_ASSERT_EQUAL_INT16(32767, AudioGain::applyOutputGain(32767, AudioGain::kUnityOutputGain));
  // Above unity is not allowed: an out-of-range gain must not amplify.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyOutputGain(1000, 60000));
  // Out-of-range volume steps clamp to the loudest entry, never index past it.
  TEST_ASSERT_EQUAL_INT16(1000, AudioGain::applyVolume(1000, 200, AudioGain::kUnityOutputGain));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_output_gain_scales_without_touching_volume);
  RUN_TEST(test_volume_step_21_is_unity_and_step_0_is_silence);
  RUN_TEST(test_volume_steps_are_monotonic_and_match_the_library_table);
  RUN_TEST(test_values_are_clamped_and_steps_beyond_the_table_are_capped);
  return UNITY_END();
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native -f test_audio_backends`
Expected: build error, `'AudioGain.h' file not found`.

- [ ] **Step 3: Write the implementation**

Create `lib/playback/AudioGain.h`:

```cpp
#pragma once

#include <algorithm>
#include <cstdint>

namespace knobify::playback {

// The one place knobify decides how loud a sample is. Both decode paths
// use it (ADR 0016, ADR 0017): the ESP32-audioI2S path applies the volume
// itself inside the library and only needs the sleep timer's output gain,
// while the Vorbis path gets raw PCM and applies both.
class AudioGain {
 public:
  // Output gain is 0..4096, where 4096 is unity -- the sleep timer fades
  // with it (ADR 0015) because the 22 volume steps are far too coarse.
  static constexpr uint16_t kUnityOutputGain = 4096;
  static constexpr uint8_t kMaxVolumeStep = 21;

  // ESP32-audioI2S's own volumetable (Audio.h): Gain() multiplies by
  // entry/64. Copied so the Vorbis path sounds identical at every step;
  // a unit test keeps the copy honest.
  static constexpr uint8_t kVolumeTable[kMaxVolumeStep + 1] = {
      0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 17, 20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};

  // For samples that already carry the volume (the library path).
  static int16_t applyOutputGain(int16_t sample, uint16_t outputGain) {
    const int32_t gain = std::min<int32_t>(outputGain, kUnityOutputGain);
    return clamp(static_cast<int32_t>(sample) * gain / kUnityOutputGain);
  }

  // For raw decoder output (the Vorbis path).
  static int16_t applyVolume(int16_t sample, uint8_t volumeStep, uint16_t outputGain) {
    const int32_t gain = std::min<int32_t>(outputGain, kUnityOutputGain);
    const int32_t step = kVolumeTable[std::min(volumeStep, kMaxVolumeStep)];
    return clamp(static_cast<int32_t>(sample) * step / 64 * gain / kUnityOutputGain);
  }

  // What the spectrum reports as the gain already applied to its samples.
  static float linearGain(uint8_t volumeStep, uint16_t outputGain) {
    const uint16_t gain = std::min<uint16_t>(outputGain, kUnityOutputGain);
    return kVolumeTable[std::min(volumeStep, kMaxVolumeStep)] / 64.0f * gain /
           kUnityOutputGain;
  }

 private:
  static int16_t clamp(int32_t value) {
    return static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
  }
};

}  // namespace knobify::playback
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pio test -e native -f test_audio_backends`
Expected: 4 tests pass.

- [ ] **Step 5: Use it in the driver, keeping today's behaviour exactly**

In `lib/drivers-audio/Esp32AudioI2SDriver.cpp`:

Add `#include "AudioGain.h"` next to the other project includes. Replace the anonymous-namespace `kUnityOutputGain`, `kVolumeTable`, `kHeadroomCompensation` and `compensate()` with the shared class, keeping the library's `>>1` compensation in the hook where it belongs:

```cpp
// ESP32-audioI2S 2.3.0's Audio::playSample() halves every sample before
// its EQ and Gain() ("half Vin so we can boost up to 6dB in filters"), so
// volume 21/21 only ever reached -6 dBFS. Doubling here, right before
// i2s_write(), makes 21 true 0 dBFS. The sleep timer's gain rides along
// via AudioGain, shared with the Vorbis path.
int16_t compensate(int16_t s, uint16_t outputGain) {
  return playback::AudioGain::applyOutputGain(
      static_cast<int16_t>(std::clamp<int32_t>(s * 2, INT16_MIN, INT16_MAX)), outputGain);
}
```

and in `readRecentSamples()`:

```cpp
  window.gain = playback::AudioGain::linearGain(
      volume_.load(), g_outputGain.load(std::memory_order_relaxed));
```

and in `setOutputGain()`:

```cpp
  g_outputGain.store(std::min<uint16_t>(gain, playback::AudioGain::kUnityOutputGain),
                     std::memory_order_relaxed);
```

- [ ] **Step 6: Verify the device build and the whole host suite**

Run: `pio run -e esp32-s3 && pio test -e native`
Expected: build SUCCESS, all tests pass (241 + 4 new).

- [ ] **Step 7: Commit**

```bash
git add lib/playback/AudioGain.h lib/drivers-audio/Esp32AudioI2SDriver.cpp test/test_audio_backends/
git commit -m "$(cat <<'EOF'
Extract the volume maths into a host-tested AudioGain

Both decode paths will need identical loudness, so the volume table, the
sleep-timer output gain and the clamping move out of the driver into
lib/playback, where the native tests can reach them. The library's own
halving compensation stays in the hook: only that path sees it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

---

### Task 2: Pick the backend by file extension, and let Ogg seek

Two pure additions, both host-tested, before any decoder exists.

**Files:**
- Create: `lib/playback/AudioBackendKind.h`
- Modify: `lib/playback/PlaybackStateMachine.h` (`canSeek()` accepts ogg)
- Modify: `test/test_audio_backends/test_audio_backends.cpp` (backend choice)
- Modify: `test/test_playback/test_playback_state_machine.cpp` (seekable formats)

**Interfaces:**
- Consumes: nothing.
- Produces: `knobify::playback::AudioBackendKind` (`enum class` with `Library`, `Vorbis`) and `knobify::playback::backendForPath(const std::string &path) -> AudioBackendKind`.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_audio_backends/test_audio_backends.cpp` (and add the two `RUN_TEST` lines in `main()`, plus `#include "AudioBackendKind.h"` and `using knobify::playback::AudioBackendKind; using knobify::playback::backendForPath;` at the top):

```cpp
void test_ogg_files_go_to_the_vorbis_backend() {
  TEST_ASSERT_TRUE(AudioBackendKind::Vorbis == backendForPath("/Music/A/B/track.ogg"));
  TEST_ASSERT_TRUE(AudioBackendKind::Vorbis == backendForPath("/Music/A/B/TRACK.OGG"));
  TEST_ASSERT_TRUE(AudioBackendKind::Vorbis == backendForPath("/x/a.oga"));
}

void test_every_other_format_goes_to_the_library_backend() {
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath("/Music/a.mp3"));
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath("/Music/a.m4a"));
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath("/Music/a.wav"));
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath("/Music/a.flac"));
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath("/Music/noextension"));
  TEST_ASSERT_TRUE(AudioBackendKind::Library == backendForPath(""));
}
```

In `test/test_playback/test_playback_state_machine.cpp`, extend the existing
`test_can_seek_only_mp3_m4a_and_wav_with_a_track` — rename it to
`test_can_seek_mp3_m4a_wav_and_ogg_with_a_track`, update its `RUN_TEST`
line, and change the ogg case from false to true:

```cpp
  sm.play({"/a.ogg"}, 0, 0);
  TEST_ASSERT_TRUE(sm.canSeek());
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native -f test_audio_backends -f test_playback`
Expected: `'AudioBackendKind.h' file not found`; after that header exists the playback test would fail on the ogg assertion.

- [ ] **Step 3: Write the implementation**

Create `lib/playback/AudioBackendKind.h`:

```cpp
#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace knobify::playback {

// Which decoder plays a file. ESP32-audioI2S 2.3.0 has no Vorbis decoder,
// so Ogg goes to knobify's own backend (ADR 0017). Opus would be a third
// value here and a second backend, nothing more.
enum class AudioBackendKind { Library, Vorbis };

inline AudioBackendKind backendForPath(const std::string &path) {
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos) return AudioBackendKind::Library;
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  // .oga is the same Vorbis stream under its audio-only name.
  if (ext == "ogg" || ext == "oga") return AudioBackendKind::Vorbis;
  return AudioBackendKind::Library;
}

}  // namespace knobify::playback
```

In `lib/playback/PlaybackStateMachine.h`, update the comment and the check in `canSeek()`:

```cpp
  // Whether the current track can be shuttled: the library seeks within
  // MP3, M4A and WAV, and knobify's own Vorbis backend seeks by sample
  // (ADR 0017). By extension, so it's known before a cued track loads.
  ...
    return ext == "mp3" || ext == "m4a" || ext == "wav" || ext == "ogg" ||
           ext == "oga";
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: all pass, including the two new backend tests and the changed seek test.

- [ ] **Step 5: Commit**

```bash
git add lib/playback/AudioBackendKind.h lib/playback/PlaybackStateMachine.h test/
git commit -m "$(cat <<'EOF'
Choose an audio backend by extension; Ogg becomes seekable

backendForPath() is the only place that decides which decoder plays a
file, so adding Opus later is a new enum value and a new backend rather
than a new branch in the driver. The state machine now offers jog/shuttle
for Ogg, which the Vorbis backend can seek by sample.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

---

### Task 3: AudioOutputStage — one way out to the DAC

Moves the spectrum ring and gain state out of file-scope globals into an object both paths use. Device-side (it writes to I2S), so its correctness is proven by Task 1's tests plus the device check at the end.

**Files:**
- Create: `lib/drivers-audio/AudioOutputStage.h`
- Create: `lib/drivers-audio/AudioOutputStage.cpp`
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.cpp` (hook and `readRecentSamples()` route through it)
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.h` (`setVolume()` tells the stage the step)

**Interfaces:**
- Consumes: `knobify::playback::AudioGain` (Task 1).
- Produces: `knobify::drivers::AudioOutputStage` with `void setVolumeStep(uint8_t step);`, `void setOutputGain(uint16_t gain);`, `uint16_t outputGain() const;`, `uint8_t volumeStep() const;`, `void noteMonoSample(int16_t mono);`, `bool writeFrames(const int16_t *interleaved, size_t frames);`, `playback::SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples, uint32_t sampleRate);`, and `AudioOutputStage &audioOutputStage();` (the single instance the weak hook can reach).

- [ ] **Step 1: Write the header**

Create `lib/drivers-audio/AudioOutputStage.h`:

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "PlaybackDriver.h"

namespace knobify::drivers {

// Where every sample leaves for the DAC, whichever decoder produced it
// (ADR 0017). Owns the volume step, the sleep timer's output gain and the
// spectrum's sample ring, so the two decode paths cannot drift apart in
// loudness or in what the analyzer sees.
//
// The ring has a single producer (whichever decode task is running, core
// 0) and a single consumer (the main loop, core 1); the producer only
// stores a sample and bumps a counter, so there is no lock in the hot
// path -- a reader racing a writer can at worst see its oldest sample
// replaced by a newer one, which is invisible in a spectrum.
class AudioOutputStage {
 public:
  void setVolumeStep(uint8_t step) { volumeStep_.store(step, std::memory_order_relaxed); }
  uint8_t volumeStep() const { return volumeStep_.load(std::memory_order_relaxed); }
  void setOutputGain(uint16_t gain);
  uint16_t outputGain() const { return outputGain_.load(std::memory_order_relaxed); }

  // The library path: its own write loop still calls i2s_write(), so this
  // only records what was heard.
  void noteMonoSample(int16_t mono);

  // knobify's own decoders: applies volume and output gain, records the
  // samples and writes them to the I2S port the library installed.
  // Blocks until the DMA buffers take the frames; false on an I2S error.
  bool writeFrames(const int16_t *interleaved, size_t frames);

  playback::SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples,
                                           uint32_t sampleRate);

 private:
  static constexpr size_t kSampleRingSize = 1024;  // Power of two.
  static constexpr size_t kWriteChunkFrames = 256;

  int16_t ring_[kSampleRingSize] = {};
  std::atomic<uint32_t> samplesWritten_{0};
  std::atomic<uint16_t> outputGain_{4096};
  std::atomic<uint8_t> volumeStep_{0};
  uint32_t lastReadCount_ = 0;
  int16_t scratch_[kWriteChunkFrames * 2] = {};
};

// One instance; the weak audio_process_i2s() hook has no other way in.
AudioOutputStage &audioOutputStage();

}  // namespace knobify::drivers
```

- [ ] **Step 2: Write the implementation**

Create `lib/drivers-audio/AudioOutputStage.cpp`:

```cpp
#include "AudioOutputStage.h"

#include <driver/i2s.h>

#include <algorithm>

#include "AudioGain.h"

namespace knobify::drivers {

namespace {
// The port ESP32-audioI2S installs in Audio's constructor; knobify's own
// decoders write to the same one rather than installing a second driver.
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
}  // namespace

void AudioOutputStage::setOutputGain(uint16_t gain) {
  outputGain_.store(std::min<uint16_t>(gain, playback::AudioGain::kUnityOutputGain),
                    std::memory_order_relaxed);
}

void AudioOutputStage::noteMonoSample(int16_t mono) {
  const uint32_t written = samplesWritten_.load(std::memory_order_relaxed);
  ring_[written & (kSampleRingSize - 1)] = mono;
  samplesWritten_.store(written + 1, std::memory_order_relaxed);
}

bool AudioOutputStage::writeFrames(const int16_t *interleaved, size_t frames) {
  const uint8_t step = volumeStep();
  const uint16_t gain = outputGain();
  size_t done = 0;
  while (done < frames) {
    const size_t chunk = std::min(frames - done, kWriteChunkFrames);
    for (size_t i = 0; i < chunk; ++i) {
      const int16_t left =
          playback::AudioGain::applyVolume(interleaved[(done + i) * 2], step, gain);
      const int16_t right =
          playback::AudioGain::applyVolume(interleaved[(done + i) * 2 + 1], step, gain);
      scratch_[i * 2] = left;
      scratch_[i * 2 + 1] = right;
      noteMonoSample(static_cast<int16_t>((static_cast<int32_t>(left) + right) / 2));
    }
    size_t bytesWritten = 0;
    if (i2s_write(kI2sPort, scratch_, chunk * 2 * sizeof(int16_t), &bytesWritten,
                  portMAX_DELAY) != ESP_OK) {
      return false;
    }
    done += chunk;
  }
  return true;
}

playback::SampleWindow AudioOutputStage::readRecentSamples(int16_t *dst,
                                                           size_t maxSamples,
                                                           uint32_t sampleRate) {
  const uint32_t written = samplesWritten_.load(std::memory_order_relaxed);
  if (written == lastReadCount_) return {};  // Paused or between tracks.
  lastReadCount_ = written;

  const size_t count = std::min<size_t>({maxSamples, kSampleRingSize, written});
  for (size_t i = 0; i < count; ++i) {
    dst[i] = ring_[(written - count + i) & (kSampleRingSize - 1)];
  }
  playback::SampleWindow window;
  window.count = count;
  window.sampleRate = sampleRate;
  window.gain = playback::AudioGain::linearGain(volumeStep(), outputGain());
  return window;
}

AudioOutputStage &audioOutputStage() {
  static AudioOutputStage stage;
  return stage;
}

}  // namespace knobify::drivers
```

- [ ] **Step 3: Route the existing path through it**

In `lib/drivers-audio/Esp32AudioI2SDriver.cpp`: delete the file-scope
`g_sampleRing`, `g_samplesWritten`, `g_outputGain` and the `kSampleRingSize`
constant, add `#include "AudioOutputStage.h"`, and rewrite the three users:

```cpp
playback::SampleWindow Esp32AudioI2SDriver::readRecentSamples(int16_t *dst,
                                                              size_t maxSamples) {
  // A plain field read, deliberately without mutex_: taking it at frame
  // rate would wait on the audio task's decode chunks, and a stale rate
  // for one frame right after a track change is harmless.
  return audioOutputStage().readRecentSamples(dst, maxSamples, audio_.getSampleRate());
}

void Esp32AudioI2SDriver::setOutputGain(uint16_t gain) {
  audioOutputStage().setOutputGain(gain);
}
```

```cpp
void audio_process_i2s(uint32_t *sample, bool *continueI2S) {
  // Packed as Gain() returns it: left in the high 16 bits, right in the low.
  auto &stage = knobify::drivers::audioOutputStage();
  const uint16_t gain = stage.outputGain();
  const int16_t left = compensate(static_cast<int16_t>(*sample >> 16), gain);
  const int16_t right = compensate(static_cast<int16_t>(*sample & 0xFFFF), gain);
  stage.noteMonoSample(static_cast<int16_t>((static_cast<int32_t>(left) + right) / 2));
  *sample = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
  *continueI2S = true;
}
```

In `lib/drivers-audio/Esp32AudioI2SDriver.h`, `setVolume()` keeps its
`volume_` member (the library needs it) and additionally tells the stage:

```cpp
  void setVolume(uint8_t volume) override {
    MutexGuard guard(mutex_);
    audio_.setVolume(volume);
    volume_.store(volume);
    audioOutputStage().setVolumeStep(volume);
  }
```

Add `#include "AudioOutputStage.h"` to that header.

- [ ] **Step 4: Build and run the whole suite**

Run: `pio run -e esp32-s3 && pio test -e native`
Expected: build SUCCESS, all host tests pass (no behaviour changed).

- [ ] **Step 5: Commit**

```bash
git add lib/drivers-audio/
git commit -m "$(cat <<'EOF'
Give both decode paths one output stage

The spectrum ring and the gain state were file-scope globals next to the
library's weak hook. They move into AudioOutputStage, which also knows how
to write frames to the I2S port the library installed -- what knobify's
own decoder will use. No behaviour change: the library path still writes
its own samples and only reports them here.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

---

### Task 4: Vendor stb_vorbis and open a file with it

No playback yet: open, report duration and sample rate, seek, close. Device-side; verified by the build and by Task 6's device checks.

**Files:**
- Create: `lib/drivers-audio/stb_vorbis.c` (vendored, from https://github.com/nothings/stb)
- Create: `lib/drivers-audio/DecoderBackend.h`
- Create: `lib/drivers-audio/VorbisBackend.h`
- Create: `lib/drivers-audio/VorbisBackend.cpp`
- Modify: `THIRD-PARTY.md`

**Interfaces:**
- Consumes: `AudioOutputStage` (Task 3).
- Produces: `knobify::drivers::DecoderBackend` (pure virtual: `bool open(const std::string &path, uint32_t startSample)`, `void close()`, `bool seekToSample(uint32_t sample)`, `uint32_t currentSample() const`, `uint32_t sampleRate() const`, `uint32_t durationSeconds() const`, `bool running() const`, `void setPaused(bool paused)`) and `knobify::drivers::VorbisBackend` implementing it.

- [ ] **Step 1: Vendor the decoder**

```bash
curl -sSfL https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c \
  -o lib/drivers-audio/stb_vorbis.c
```

Prepend this comment to the file (keep everything below it unchanged):

```c
// Vendored from https://github.com/nothings/stb (public domain / MIT).
// knobify uses it for Ogg Vorbis playback because ESP32-audioI2S 2.3.0 has
// no Vorbis decoder -- see docs/adr/0017-two-audio-decode-paths.md.
// Measured on the device 2026-09-15: 2.5x realtime, ~180 KB working set,
// needs a task stack of ~32 KB.
```

- [ ] **Step 2: Write the interface**

Create `lib/drivers-audio/DecoderBackend.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace knobify::drivers {

// A decoder knobify drives itself, as opposed to the ESP32-audioI2S path
// (ADR 0017). Positions are sample indices: that is what a Vorbis stream
// can seek to, and PlaybackDriver's position is opaque above the driver.
class DecoderBackend {
 public:
  virtual ~DecoderBackend() = default;

  // Opens `path` and starts producing audio from `startSample`.
  virtual bool open(const std::string &path, uint32_t startSample) = 0;
  virtual void close() = 0;
  virtual bool seekToSample(uint32_t sample) = 0;
  // Where playback currently is; 0 when nothing is open.
  virtual uint32_t currentSample() const = 0;
  virtual uint32_t sampleRate() const = 0;
  virtual uint32_t durationSeconds() const = 0;
  // False once the stream ended or nothing is open.
  virtual bool running() const = 0;
  virtual void setPaused(bool paused) = 0;
};

}  // namespace knobify::drivers
```

- [ ] **Step 3: Write the backend's open/close/metadata half**

Create `lib/drivers-audio/VorbisBackend.h`:

```cpp
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "DecoderBackend.h"

struct stb_vorbis;

namespace knobify::drivers {

// Ogg Vorbis playback on knobify's own decode path (ADR 0017): stb_vorbis
// reading through the filesystem, decoding on a task of its own because it
// needs about 32 KB of stack -- far more than the shared audio task has,
// and only worth committing while an Ogg actually plays. Its working set
// (~180 KB) lands in PSRAM through the allocator setup in setup().
class VorbisBackend : public DecoderBackend {
 public:
  static constexpr uint32_t kTaskStackBytes = 32 * 1024;
  static constexpr size_t kFramesPerChunk = 1024;

  ~VorbisBackend() override { close(); }

  bool open(const std::string &path, uint32_t startSample) override;
  void close() override;
  bool seekToSample(uint32_t sample) override;
  uint32_t currentSample() const override {
    return currentSample_.load(std::memory_order_relaxed);
  }
  uint32_t sampleRate() const override { return sampleRate_; }
  uint32_t durationSeconds() const override { return durationSeconds_; }
  bool running() const override { return running_.load(std::memory_order_relaxed); }
  void setPaused(bool paused) override {
    paused_.store(paused, std::memory_order_relaxed);
  }

 private:
  static void taskTrampoline(void *self);
  void decodeLoop();

  stb_vorbis *stream_ = nullptr;
  TaskHandle_t task_ = nullptr;
  int16_t *frames_ = nullptr;  // kFramesPerChunk * 2, PSRAM.
  uint32_t sampleRate_ = 0;
  uint32_t durationSeconds_ = 0;
  int channels_ = 0;
  std::atomic<uint32_t> currentSample_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<uint32_t> seekRequest_{kNoSeek};

  static constexpr uint32_t kNoSeek = UINT32_MAX;
};

}  // namespace knobify::drivers
```

Create `lib/drivers-audio/VorbisBackend.cpp` with the open/close half (the
decode task arrives in Task 5; `decodeLoop()` is written there):

```cpp
#include "VorbisBackend.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "AudioOutputStage.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace knobify::drivers {

bool VorbisBackend::open(const std::string &path, uint32_t startSample) {
  close();
  // The SD card is mounted at /sdcard; stb_vorbis reads it through the
  // normal filesystem rather than knobify's RawFile abstraction, which
  // exists for tag parsing on the host.
  const std::string fsPath = "/sdcard" + path;
  int error = 0;
  stream_ = stb_vorbis_open_filename(fsPath.c_str(), &error, nullptr);
  if (!stream_) {
    Serial.printf("[vorbis] open failed (%d): %s\n", error, path.c_str());
    return false;
  }
  const stb_vorbis_info info = stb_vorbis_get_info(stream_);
  sampleRate_ = info.sample_rate;
  channels_ = info.channels;
  durationSeconds_ =
      static_cast<uint32_t>(stb_vorbis_stream_length_in_seconds(stream_) + 0.5f);
  frames_ = static_cast<int16_t *>(
      heap_caps_malloc(kFramesPerChunk * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (!frames_) {
    Serial.println("[vorbis] out of memory for the frame buffer");
    close();
    return false;
  }
  if (startSample != 0) stb_vorbis_seek(stream_, startSample);
  currentSample_.store(startSample, std::memory_order_relaxed);
  stopRequested_.store(false, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);
  return true;
}

void VorbisBackend::close() {
  if (task_) {
    stopRequested_.store(true, std::memory_order_relaxed);
    // The decode loop checks the flag between chunks (at most ~23 ms of
    // audio) and deletes itself; wait for it before freeing anything.
    while (running_.load(std::memory_order_relaxed)) delay(2);
    task_ = nullptr;
  }
  if (stream_) {
    stb_vorbis_close(stream_);
    stream_ = nullptr;
  }
  if (frames_) {
    heap_caps_free(frames_);
    frames_ = nullptr;
  }
  sampleRate_ = 0;
  durationSeconds_ = 0;
  currentSample_.store(0, std::memory_order_relaxed);
  running_.store(false, std::memory_order_relaxed);
}

bool VorbisBackend::seekToSample(uint32_t sample) {
  if (!stream_) return false;
  // Applied by the decode task between chunks: stb_vorbis is not safe to
  // call from two tasks at once.
  seekRequest_.store(sample, std::memory_order_relaxed);
  return true;
}

}  // namespace knobify::drivers
```

- [ ] **Step 4: Build**

Run: `pio run -e esp32-s3`
Expected: SUCCESS (the backend is compiled but not yet referenced).

- [ ] **Step 5: Record the dependency**

In `THIRD-PARTY.md`, add a row to the table:

```markdown
| [stb_vorbis](https://github.com/nothings/stb) (vendored) | Ogg Vorbis decoding | Public domain / MIT |
```

- [ ] **Step 6: Commit**

```bash
git add lib/drivers-audio/ THIRD-PARTY.md
git commit -m "$(cat <<'EOF'
Vendor stb_vorbis and open Ogg files with it

DecoderBackend is the slot knobify's own decoders fill; VorbisBackend
opens a stream, reports its exact duration and sample rate and accepts
seek requests. No audio comes out yet -- the decode task is next.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

---

### Task 5: Decode on a task and dispatch from the driver

**Files:**
- Modify: `lib/drivers-audio/VorbisBackend.cpp` (the decode task)
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.h` (dispatch by backend)
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.cpp` (nothing new; check it still compiles)

**Interfaces:**
- Consumes: `DecoderBackend`/`VorbisBackend` (Task 4), `backendForPath()` (Task 2), `AudioOutputStage` (Task 3).
- Produces: a `PlaybackDriver` that plays Ogg through `VorbisBackend` and everything else through ESP32-audioI2S.

- [ ] **Step 1: Write the decode task**

In `lib/drivers-audio/VorbisBackend.cpp`, add the task plumbing. Start it at the end of `open()`, just before `return true;`:

```cpp
  // Pinned to core 0 at priority 3, like the library's audio task, so it
  // never competes with LVGL on core 1.
  if (xTaskCreatePinnedToCore(&VorbisBackend::taskTrampoline, "vorbis",
                              kTaskStackBytes, this, /*priority=*/3, &task_,
                              /*core=*/0) != pdPASS) {
    Serial.println("[vorbis] could not start the decode task");
    close();
    return false;
  }
```

and implement the loop:

```cpp
void VorbisBackend::taskTrampoline(void *self) {
  static_cast<VorbisBackend *>(self)->decodeLoop();
}

void VorbisBackend::decodeLoop() {
  auto &stage = audioOutputStage();
  while (!stopRequested_.load(std::memory_order_relaxed)) {
    const uint32_t seekTo = seekRequest_.exchange(kNoSeek, std::memory_order_relaxed);
    if (seekTo != kNoSeek) {
      stb_vorbis_seek(stream_, seekTo);
      currentSample_.store(seekTo, std::memory_order_relaxed);
    }
    if (paused_.load(std::memory_order_relaxed)) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const int frames = stb_vorbis_get_samples_short_interleaved(
        stream_, 2, frames_, static_cast<int>(kFramesPerChunk * 2));
    if (frames <= 0) break;  // End of stream.
    // A mono file decodes to two identical channels when 2 is requested,
    // so the output stage always gets interleaved stereo.
    if (!stage.writeFrames(frames_, static_cast<size_t>(frames))) break;
    currentSample_.fetch_add(static_cast<uint32_t>(frames), std::memory_order_relaxed);
  }
  running_.store(false, std::memory_order_relaxed);
  vTaskDelete(nullptr);
}
```

- [ ] **Step 2: Dispatch in the driver**

In `lib/drivers-audio/Esp32AudioI2SDriver.h`, add the includes
(`#include "AudioBackendKind.h"`, `#include "VorbisBackend.h"`), a member
`VorbisBackend vorbis_;` plus `bool vorbisActive_ = false;`, and route
every entry point. Replace the existing `playFileAt`, `pause`, `resume`,
`stop`, `seekByMs`, `filePosition`, `durationSeconds` and `isRunning`
bodies with these:

```cpp
  bool playFileAt(const std::string &path, uint32_t position) override {
    // Only ever one decoder: stop the other before starting this one.
    stop();
    if (playback::backendForPath(path) == playback::AudioBackendKind::Vorbis) {
      MutexGuard guard(mutex_);
      audio_.stopSong();
      vorbisActive_ = vorbis_.open(path, position);
      if (vorbisActive_) {
        // The library set the port's clock for its own last track.
        audio_.setSampleRate(vorbis_.sampleRate());
      }
      return vorbisActive_;
    }
    timing_ = readTrackTiming(path);
    MutexGuard guard(mutex_);
    paused_ = false;
    bool ok = audio_.connecttoFS(SD_MMC, path.c_str(), position);
    Serial.printf("Esp32AudioI2SDriver::playFile('%s') -> connecttoFS=%s\n",
                  path.c_str(), ok ? "OK" : "FAILED");
    return ok;
  }

  void pause() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) {
      vorbis_.setPaused(true);
      paused_ = true;
      return;
    }
    if (!paused_) {
      audio_.pauseResume();
      paused_ = true;
    }
  }

  void resume() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) {
      vorbis_.setPaused(false);
      paused_ = false;
      return;
    }
    if (paused_) {
      audio_.pauseResume();
      paused_ = false;
    }
  }

  void stop() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) {
      vorbis_.close();
      vorbisActive_ = false;
    }
    audio_.stopSong();
    paused_ = false;
  }

  bool isRunning() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) return vorbis_.running();
    return audio_.isRunning();
  }

  uint32_t durationSeconds() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) return vorbis_.durationSeconds();
    if (timing_.durationSeconds != 0) return timing_.durationSeconds;
    return audio_.getAudioFileDuration();
  }

  uint32_t filePosition() override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) return vorbis_.currentSample();
    uint32_t reader = audio_.getFilePos();
    if (reader == 0) return 0;  // Nothing loaded.
    int64_t heard = static_cast<int64_t>(reader) - audio_.inBufferFilled();
    int64_t start = audio_.getAudioDataStartPos();
    return static_cast<uint32_t>(heard < start ? start : heard);
  }
```

and at the top of `seekByMs()`, before the existing byte maths:

```cpp
  bool seekByMs(int32_t deltaMs) override {
    MutexGuard guard(mutex_);
    if (vorbisActive_) {
      const uint32_t rate = vorbis_.sampleRate();
      if (rate == 0) return false;
      const int64_t delta = static_cast<int64_t>(deltaMs) * rate / 1000;
      const int64_t target = static_cast<int64_t>(vorbis_.currentSample()) + delta;
      return vorbis_.seekToSample(static_cast<uint32_t>(target < 0 ? 0 : target));
    }
```

Note for the implementer: `stop()` takes the mutex, and `playFileAt()`
calls it before taking the mutex itself — do not hold it across both, or
the driver deadlocks.

- [ ] **Step 3: Build and run the host suite**

Run: `pio run -e esp32-s3 && pio test -e native`
Expected: build SUCCESS, all host tests pass.

- [ ] **Step 4: Commit**

```bash
git add lib/drivers-audio/
git commit -m "$(cat <<'EOF'
Play Ogg Vorbis on knobify's own decode path

VorbisBackend decodes on a task of its own and writes through the shared
output stage, so volume, the sleep-timer fade and the spectrum behave as
they do for MP3. The driver picks the backend by extension and keeps the
two decoders mutually exclusive; positions are sample indices for Ogg,
which is what makes seeking and resume exact.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

---

### Task 6: ADR 0017, the debt, and the device verification

**Files:**
- Create: `docs/adr/0017-two-audio-decode-paths.md`
- Modify: `docs/adr/README.md` (index row)
- Modify: `README.md` (features list and backlog)
- Modify: `AGENTS.md` (one gotcha entry)
- Modify: `lib/drivers-audio/Esp32AudioI2SDriver.h` (pointer to the ADR in the class comment)

- [ ] **Step 1: Write the ADR**

Create `docs/adr/0017-two-audio-decode-paths.md`:

```markdown
# 0017: Two audio decode paths

## Status

Accepted — 2026-09-16. Follows ADR 0016 (native formats) and records the
debt that Ogg Vorbis support creates.

## Context

ESP32-audioI2S 2.3.0 decodes MP3, AAC/M4A, WAV and FLAC, but has no Vorbis
decoder: it rejects Ogg streams that aren't Ogg-FLAC. The user's audio
dramas — 1.5 GB, 29 files of about 23 minutes — are Vorbis, and a player
published as open source should handle a free format.

The library that does decode Vorbis is upstream ESP32-audioI2S 3.x, which
requires Arduino-ESP32 3.x / ESP-IDF 5. This board fought us on nearly
every layer that migration would touch: octal PSRAM init, the SDMMC card
layout, TinyUSB mass storage, the USB CDC console, touch calibration, a
non-standard rotary encoder, and a display driver vendored against
Arduino_GFX 1.4.9 because newer releases need the 3.x core. All of it
would need re-validating for one format.

## Decision

Keep the library for its formats and add a second decode path for Vorbis:
stb_vorbis behind `DecoderBackend`, decoding on its own task and writing
through `AudioOutputStage`, the single place where samples reach the DAC.
`Esp32AudioI2SDriver` picks the path by file extension
(`playback::backendForPath()`) and keeps the two mutually exclusive.

## Consequences

- Two decode paths to keep in step. `AudioGain` (host-tested) and
  `AudioOutputStage` are the guard: volume, the sleep-timer fade and the
  spectrum come from one implementation, so the paths cannot drift in
  loudness or in what the analyzer sees.
- Ogg positions are sample indices, not byte offsets. The value stays
  opaque above `PlaybackDriver`, so resume and jog/shuttle work unchanged.
- While an Ogg plays, ~180 KB of PSRAM and a 32 KB task stack are in use;
  both are released when it stops. PSRAM is shared with the LVGL pool.
- Opus is a new backend, not a new structure — but see below.

## Revisit when

1. **Opus is wanted.** A third decode path would make this a pattern
   rather than an exception — that is the point to move to a library that
   decodes everything.
2. **The project moves to Arduino-ESP32 3.x for any other reason.** The
   upgrade to ESP32-audioI2S 3.x is nearly free then, and this debt should
   be paid in the same migration rather than left behind it.
3. **ESP32-audioI2S 2.3.0 costs us again.** It already called a weak hook
   without a null check and crashed M4A playback (2026-09-16, AGENTS.md).
   A second incident of that kind is reason enough.
4. **Yearly**, whichever comes first — next review due 2027-09.
```

- [ ] **Step 2: Link it from the places that matter**

- `docs/adr/README.md`: add `| [0017](0017-two-audio-decode-paths.md) | Two audio decode paths (Vorbis) | Accepted |`.
- `README.md`: move Ogg Vorbis out of the backlog into the feature list, and note that embedded Ogg cover art is still unsupported.
- `AGENTS.md`: add a bullet saying knobify has two decode paths, that `AudioGain`/`AudioOutputStage` keep them consistent, and that Ogg positions are sample indices rather than byte offsets.
- `lib/drivers-audio/Esp32AudioI2SDriver.h`: extend the class comment with one sentence pointing at ADR 0017.

- [ ] **Step 3: Commit**

```bash
git add docs/ README.md AGENTS.md lib/drivers-audio/
git commit -m "$(cat <<'EOF'
Record the two-decode-path debt as ADR 0017

Vorbis playback means knobify now decodes audio two ways. The ADR states
why (the pinned library has no Vorbis decoder, the alternative is a whole
core migration), what it costs, and the four conditions under which the
decision should be revisited.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01UAtxf3DVXm8T7XxgwCecB1
EOF
)"
```

- [ ] **Step 4: Ask the user before flashing**

Ask: "Vorbis playback is implemented and the host suite passes. May I flash the device and run the checks?" Wait for a yes — the user uses the device for transfers.

- [ ] **Step 5: Verify on the device**

Flash with `./scripts/flash-primary-mcu.sh`, then with the serial helpers
(`TAP x y`, `KNOB n`, `INFO`, `SCREENSHOT` — see AGENTS.md) confirm:

1. A Hörspiel under `/Music/Hörspiele/...` plays; the Now Playing screen
   shows a duration of about 23:00, and the elapsed time advances.
2. Jog/shuttle moves within the track and playback continues from there.
3. Pause and resume work; `INFO` shows the internal heap unchanged after
   several start/stop cycles (the decode task must be freed each time).
4. The spectrum reacts, and volume steps sound like MP3 at the same
   setting.
5. Ogg → MP3 → Ogg in one session leaves no stuck task and no crash
   (`INFO` reset reason stays 0/1, never 4 or 6).

If anything crashes, `scripts/read-coredump.sh` decodes the panic; a hang
needs a `-DKNOBIFY_LOOP_WDT` build first.

- [ ] **Step 6: Commit any fixes the device checks require, then report**

Report the measured behaviour (CPU headroom, heap after cycles) to the
user and update ADR 0017's Consequences if reality differed.
