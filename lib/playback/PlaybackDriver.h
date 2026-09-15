#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace knobify::playback {

// What PlaybackDriver::readRecentSamples() copied out. `count == 0` means
// no new audio since the last read (paused, stopped, between tracks).
struct SampleWindow {
  size_t count = 0;
  uint32_t sampleRate = 0;
  // Linear volume gain already applied to the samples (0 when muted).
  float gain = 0.0f;
};

// Hardware-facing surface PlaybackStateMachine needs from the audio
// stack. The concrete adapter (Esp32AudioI2SDriver, lib/drivers-audio/)
// wraps ESP32-audioI2S and is the only place that library's headers are
// included outside src/main.cpp.
class PlaybackDriver {
 public:
  virtual ~PlaybackDriver() = default;

  virtual bool playFile(const std::string &path) = 0;
  // Starts `path` at a byte offset previously read from filePosition() --
  // how a track resumes after a reboot (ADR 0012). 0 plays from the start.
  virtual bool playFileAt(const std::string &path, uint32_t filePosition) = 0;
  // Byte offset the decoder has read the current file up to; 0 if nothing
  // is loaded. Only good for passing back to playFileAt().
  virtual uint32_t filePosition() = 0;
  // Jumps the decoder by `deltaMs` within the current file (negative =
  // back), for jog/shuttle (ADR 0013). Approximate: converted to bytes via
  // the average bitrate. False if nothing seekable is loaded.
  virtual bool seekByMs(int32_t deltaMs) = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void stop() = 0;
  virtual void setVolume(uint8_t volume) = 0;  // 0-21, ESP32-audioI2S's range.
  // A fine gain on top of the volume, 0..4096 (4096 = unity), applied per
  // sample so it can change smoothly -- the sleep timer's fade (ADR 0015).
  // The volume's 22 steps are far too coarse for that.
  virtual void setOutputGain(uint16_t gain) = 0;
  virtual bool isRunning() = 0;
  // Current track's total duration as reported by the decoder; 0 if
  // unknown (not yet parsed, or the format doesn't expose it).
  virtual uint32_t durationSeconds() = 0;
  // Copies up to `maxSamples` of the most recent mono samples sent to the
  // DAC into `dst`, oldest first -- the spectrum analyzer's input.
  virtual SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples) = 0;
  // Pumps the underlying codec; call every loop() iteration.
  virtual void loop() = 0;
};

}  // namespace knobify::playback
