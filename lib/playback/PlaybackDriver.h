#pragma once

#include <cstdint>
#include <string>

namespace knobify::playback {

// Hardware-facing surface PlaybackStateMachine needs from the audio
// stack. The concrete adapter (Esp32AudioI2SDriver, lib/drivers-audio/)
// wraps ESP32-audioI2S and is the only place that library's headers are
// included outside src/main.cpp.
class PlaybackDriver {
 public:
  virtual ~PlaybackDriver() = default;

  virtual bool playFile(const std::string &path) = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void stop() = 0;
  virtual void setVolume(uint8_t volume) = 0;  // 0-21, ESP32-audioI2S's range.
  virtual bool isRunning() = 0;
  // Current track's total duration as reported by the decoder; 0 if
  // unknown (not yet parsed, or the format doesn't expose it).
  virtual uint32_t durationSeconds() = 0;
  // Pumps the underlying codec; call every loop() iteration.
  virtual void loop() = 0;
};

}  // namespace knobify::playback
