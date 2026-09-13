#pragma once

#include <Audio.h>
#include <SD_MMC.h>

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
class Esp32AudioI2SDriver : public playback::PlaybackDriver {
 public:
  void begin() { audio_.setPinout(kAudioBclkPin, kAudioLrcPin, kAudioDoutPin); }

  bool playFile(const std::string &path) override {
    paused_ = false;
    bool ok = audio_.connecttoFS(SD_MMC, path.c_str());
    // TEMPORARY DIAGNOSTIC (2026-09-12): investigating "play does
    // nothing, no sound" reports on real hardware -- see AGENTS.md.
    // audio_info() (defined in Esp32AudioI2SDriver.cpp) additionally
    // surfaces the library's own internal status/error messages, which
    // are otherwise silently discarded.
    Serial.printf("Esp32AudioI2SDriver::playFile('%s') -> connecttoFS=%s\n",
                  path.c_str(), ok ? "OK" : "FAILED");
    return ok;
  }

  void pause() override {
    if (!paused_) {
      audio_.pauseResume();
      paused_ = true;
    }
  }

  void resume() override {
    if (paused_) {
      audio_.pauseResume();
      paused_ = false;
    }
  }

  void stop() override {
    audio_.stopSong();
    paused_ = false;
  }

  void setVolume(uint8_t volume) override { audio_.setVolume(volume); }

  bool isRunning() override { return audio_.isRunning(); }

  void loop() override { audio_.loop(); }

 private:
  Audio audio_;
  bool paused_ = false;
};

}  // namespace knobify::drivers
