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
