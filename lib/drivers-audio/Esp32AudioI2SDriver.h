#pragma once

#include <Audio.h>
#include <SD_MMC.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "PlaybackDriver.h"

namespace knobify::drivers {

// I2S pins for the PCM5100A DAC, per device.md's pinout.
constexpr uint8_t kAudioBclkPin = 39;
constexpr uint8_t kAudioLrcPin = 40;
constexpr uint8_t kAudioDoutPin = 41;

// Wraps ESP32-audioI2S's Audio class behind PlaybackDriver -- the only
// file including <Audio.h> outside src/main.cpp (ADR 0001,
// coding-guidelines.md hardware/logic separation). The underlying
// library only exposes a combined pauseResume() toggle, not separate
// pause()/resume() methods, so this driver tracks whether it's currently
// paused and only calls it when the state actually needs to flip.
//
// ESP32-audioI2S's Audio::loop() is a cooperative decoder: it must be
// called very frequently or the I2S buffer underruns audibly. Running it
// from src/main.cpp's loop() (as this class originally did) meant any
// stretch of blocking work elsewhere in that same loop -- most notably
// LvglGlue's synchronous QSPI display flush during heavy LVGL redraw
// activity (an animation, fast list scrolling) -- starved loop() calls
// and produced audible stutter. Confirmed on real hardware 2026-09-13:
// stutter appeared exactly during the lock screen's pulsing hint
// animation and while scrolling long lists, and disappeared turning the
// encoder (which doesn't trigger extra redraws). Fixed by running
// Audio::loop() continuously on its own FreeRTOS task pinned to the
// otherwise-idle second core (this project uses neither WiFi nor
// Bluetooth), decoupling it entirely from display/UI timing.
//
// audio_ is not thread-safe on its own, so every method here -- the
// audio task's repeated loop() call included -- takes mutex_ for the
// duration of its call into audio_. Calls from the main task (playFile,
// pause, resume, stop, setVolume, isRunning) are all short, so they
// don't meaningfully compete with the audio task's per-chunk loop()
// calls for the mutex.
class Esp32AudioI2SDriver : public playback::PlaybackDriver {
 public:
  // Defined in Esp32AudioI2SDriver.cpp, not inline, on purpose: that file
  // also defines the library's weak audio_process_i2s hook. Weak references never pull an object out of a
  // static library archive, so without this strong reference from
  // main.cpp the linker silently dropped those hooks entirely.
  void begin();

  bool playFile(const std::string &path) override { return playFileAt(path, 0); }

  // The library corrects the offset to a frame boundary per codec itself.
  bool playFileAt(const std::string &path, uint32_t filePosition) override {
    MutexGuard guard(mutex_);
    paused_ = false;
    bool ok = audio_.connecttoFS(SD_MMC, path.c_str(), filePosition);
    // TEMPORARY DIAGNOSTIC (2026-09-12): investigating "play does
    // nothing, no sound" reports on real hardware -- see AGENTS.md.
    Serial.printf("Esp32AudioI2SDriver::playFile('%s') -> connecttoFS=%s\n",
                  path.c_str(), ok ? "OK" : "FAILED");
    return ok;
  }

  void pause() override {
    MutexGuard guard(mutex_);
    if (!paused_) {
      audio_.pauseResume();
      paused_ = true;
    }
  }

  void resume() override {
    MutexGuard guard(mutex_);
    if (paused_) {
      audio_.pauseResume();
      paused_ = false;
    }
  }

  void stop() override {
    MutexGuard guard(mutex_);
    audio_.stopSong();
    paused_ = false;
  }

  void setVolume(uint8_t volume) override {
    MutexGuard guard(mutex_);
    audio_.setVolume(volume);
    volume_.store(volume);
  }

  bool isRunning() override {
    MutexGuard guard(mutex_);
    return audio_.isRunning();
  }

  uint32_t filePosition() override {
    MutexGuard guard(mutex_);
    return audio_.getFilePos();
  }

  // setTimeOffset() only takes whole seconds -- too coarse for 2× cue
  // (ADR 0013) -- so convert ms to bytes with the average bitrate and use
  // setFilePos(), which the decoder applies on its next chunk (and
  // re-aligns to a frame boundary itself). getFilePos() is the reader
  // position, not what's actually heard: with PSRAM the library's input
  // buffer holds up to ~283 KB of read-ahead (~17.7 s at 128 kbps), so
  // seeking from getFilePos() would drift forward by a whole buffer's
  // worth on every call. Audio::stopSong() faces the same problem and
  // solves it the same way (Audio.cpp ~2281): subtract inBufferFilled()
  // to get the position actually being decoded/heard.
  bool seekByMs(int32_t deltaMs) override {
    MutexGuard guard(mutex_);
    uint32_t avgBitrate = audio_.getBitRate(true);
    if (avgBitrate == 0) return false;
    int64_t bytes = static_cast<int64_t>(deltaMs) * avgBitrate / 8000;
    int64_t heard = static_cast<int64_t>(audio_.getFilePos()) -
                    static_cast<int64_t>(audio_.inBufferFilled());
    int64_t target = heard + bytes;
    int64_t start = audio_.getAudioDataStartPos();
    // setFilePos() clamps to m_audioDataStart itself, but a value of
    // exactly 0 is treated as "no resume pending" (m_resumeFilePos is
    // checked for truthiness) and silently ignored -- and an MP3 with no
    // ID3 tag has a data start of 0. Floor at 1 so a rewind to the very
    // start of such a file still takes effect.
    if (start < 1) start = 1;
    int64_t end = audio_.getFileSize();
    if (target < start) target = start;
    if (target > end) target = end;
    return audio_.setFilePos(static_cast<uint32_t>(target));
  }

  uint32_t durationSeconds() override {
    MutexGuard guard(mutex_);
    return audio_.getAudioFileDuration();
  }

  // Defined in the .cpp, next to the audio_process_i2s hook that fills the
  // sample ring it reads from.
  playback::SampleWindow readRecentSamples(int16_t *dst,
                                           size_t maxSamples) override;

  // No-op: the audio task (started in begin()) services the codec
  // directly and continuously now, independent of src/main.cpp's loop()
  // timing. Kept on the interface (rather than removed) so
  // PlaybackDriver callers don't need to special-case this
  // implementation, and so host-side FakeDriver-style tests can still
  // implement it meaningfully if they ever need to.
  void loop() override {}

 private:
  // RAII take/give -- every method above (and the audio task's own loop
  // call) needs the same "hold mutex_ for the duration of one audio_
  // call" shape.
  struct MutexGuard {
    explicit MutexGuard(SemaphoreHandle_t m) : m_(m) {
      xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~MutexGuard() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  static void audioTaskTrampoline(void *self) {
    static_cast<Esp32AudioI2SDriver *>(self)->audioTaskLoop();
  }

  [[noreturn]] void audioTaskLoop() {
    for (;;) {
      {
        MutexGuard guard(mutex_);
        audio_.loop();
      }
      // Yields to the idle task (feeds core 0's watchdog) between
      // chunks; short enough not to reintroduce underrun risk, long
      // enough not to busy-spin.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  Audio audio_;
  bool paused_ = false;
  // Mirrors the last setVolume() so readRecentSamples() can report the
  // gain without taking mutex_ at spectrum frame rate.
  std::atomic<uint8_t> volume_{0};
  uint32_t lastSampleCount_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
};

}  // namespace knobify::drivers
