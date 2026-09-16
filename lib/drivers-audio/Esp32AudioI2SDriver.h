#pragma once

#include <Audio.h>
#include <SD_MMC.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "AudioOutputStage.h"
#include "Mp3Duration.h"
#include "Mp4Parser.h"
#include "PlaybackDriver.h"
#include "SdRawFile.h"

namespace knobify::drivers {

// I2S pins for the PCM5100A DAC, per device.md's pinout.
constexpr uint8_t kAudioBclkPin = 39;
constexpr uint8_t kAudioLrcPin = 40;
constexpr uint8_t kAudioDoutPin = 41;

// Decoder input buffer in PSRAM -- see Esp32AudioI2SDriver::begin().
constexpr int kInputBufferBytes = 64 * 1024;

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
    timing_ = readTrackTiming(path);
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
    audioOutputStage().setVolumeStep(volume);
  }

  // Lock-free: read per sample on the audio task (audio_process_i2s()).
  void setOutputGain(uint16_t gain) override;

  bool isRunning() override {
    MutexGuard guard(mutex_);
    return audio_.isRunning();
  }

  // Where the listener actually is, for resume (ADR 0012): getFilePos() is
  // the reader, a whole input buffer ahead of the audio, so resuming there
  // skipped up to that buffer's worth (~4 s of a 128 kbps MP3 at 64 KB, ~17 s
  // with the library's old 300 KB default). Subtracting what's still
  // buffered is how the library itself reports the position in stopSong(),
  // and matches seekByMs() below.
  uint32_t filePosition() override {
    MutexGuard guard(mutex_);
    uint32_t reader = audio_.getFilePos();
    if (reader == 0) return 0;  // Nothing loaded.
    int64_t heard = static_cast<int64_t>(reader) - audio_.inBufferFilled();
    int64_t start = audio_.getAudioDataStartPos();
    return static_cast<uint32_t>(heard < start ? start : heard);
  }

  // setTimeOffset() only takes whole seconds -- too coarse for 2× cue
  // (ADR 0013) -- so convert ms to bytes with the average bitrate and use
  // setFilePos(), which the decoder applies on its next chunk (and
  // re-aligns to a frame boundary itself). getFilePos() is the reader
  // position, not what's actually heard: it runs a whole input buffer
  // ahead (kInputBufferBytes; the library's 300 KB default was ~17.7 s at
  // 128 kbps), so seeking from getFilePos() would drift forward by that
  // much on every call. Audio::stopSong() faces the same problem and
  // solves it the same way (Audio.cpp ~2281): subtract inBufferFilled()
  // to get the position actually being decoded/heard.
  bool seekByMs(int32_t deltaMs) override {
    MutexGuard guard(mutex_);
    int64_t start = audio_.getAudioDataStartPos();
    int64_t end = audio_.getFileSize();
    // An M4A's moov atom (tags, cover, sample tables) can sit after the
    // audio, so the file size overstates the audio data.
    if (timing_.dataEnd != 0) {
      start = timing_.dataStart;
      end = timing_.dataEnd;
    }
    // With the exact duration known, the true average bitrate is the audio
    // data over its length. The library's own average only covers the
    // first ~200 frames, so for VBR files it is off -- most right after a
    // track starts -- and jumps came out too long or too short.
    uint32_t avgBitrate =
        timing_.durationSeconds != 0 && end > start
            ? static_cast<uint32_t>((end - start) * 8 / timing_.durationSeconds)
            : audio_.getBitRate(true);
    if (avgBitrate == 0) return false;
    int64_t bytes = static_cast<int64_t>(deltaMs) * avgBitrate / 8000;
    int64_t heard = static_cast<int64_t>(audio_.getFilePos()) -
                    static_cast<int64_t>(audio_.inBufferFilled());
    int64_t target = heard + bytes;
    // setFilePos() clamps to m_audioDataStart itself, but a value of
    // exactly 0 is treated as "no resume pending" (m_resumeFilePos is
    // checked for truthiness) and silently ignored -- and an MP3 with no
    // ID3 tag has a data start of 0. Floor at 1 so a rewind to the very
    // start of such a file still takes effect.
    if (start < 1) start = 1;
    if (target < start) target = start;
    if (target > end) target = end;
    return audio_.setFilePos(static_cast<uint32_t>(target));
  }

  // The exact duration from the MP3's VBR header or the M4A's movie header
  // when there is one; otherwise the library's bitrate-based estimate (which
  // for VBR files starts too long and corrects itself over the first seconds
  // -- see Mp3Duration.h, Mp4Parser.h).
  uint32_t durationSeconds() override {
    if (timing_.durationSeconds != 0) return timing_.durationSeconds;
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

  // What the decoder's own estimates get wrong, read from the file itself.
  struct TrackTiming {
    uint32_t durationSeconds = 0;  // 0 when unknown.
    size_t dataStart = 0;          // Audio data byte range; both 0 when
    size_t dataEnd = 0;            // the library's own values are right.
  };

  // One extra open per track start, before the decoder opens the file; not
  // under mutex_: it's a separate handle, and SD_MMC serializes card access
  // itself.
  static TrackTiming readTrackTiming(const std::string &path) {
    TrackTiming timing;
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return timing;
    std::string ext = path.substr(dot + 1);
    for (char &c : ext) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (ext != "mp3" && ext != "m4a") return timing;
    fs::File file = SD_MMC.open(path.c_str());
    if (!file) return timing;
    SdRawFile raw(std::move(file));
    if (ext == "mp3") {
      timing.durationSeconds = library::Mp3Duration::readSeconds(raw);
      return timing;
    }
    library::Mp4Info info = library::Mp4Parser::parse(raw);
    timing.durationSeconds = (info.durationMs + 500) / 1000;
    timing.dataStart = info.mdatStart;
    timing.dataEnd = info.mdatEnd;
    return timing;
  }

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
  // Of the current track (set in playFileAt(), read by durationSeconds() and
  // seekByMs() -- all on the main task).
  TrackTiming timing_;
  // Mirrors the last setVolume() so readRecentSamples() can report the
  // gain without taking mutex_ at spectrum frame rate.
  std::atomic<uint8_t> volume_{0};
  uint32_t lastSampleCount_ = 0;
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
};

}  // namespace knobify::drivers
