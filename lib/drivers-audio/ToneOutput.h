#pragma once

#include <cstddef>
#include <cstdint>

#include "BlipPlayer.h"

namespace knobify::drivers {

// Gets Pong's blips to the DAC when *nothing is playing* (ADR 0022).
//
// While a track runs, blips are mixed into it by AudioOutputStage and the
// library path's audio_process_i2s() hook, and this class does nothing.
// But neither of those is called when the player is stopped -- which is
// the usual state for someone who opened a game -- so something has to
// push samples itself. This task does, and only then: it watches
// AudioOutputStage's sample counter and stands down the moment a decoder
// starts producing, so the two never fight over the I2S port for longer
// than one small buffer.
//
// Deliberately its own FreeRTOS task on core 0 rather than work done in
// loop(): AudioOutputStage::writeFrames() blocks until the DMA buffers
// take the frames, and blocking loop() is what makes LVGL stutter and
// trips the loop watchdog (AGENTS.md).
class ToneOutput : public games::BlipPlayer {
 public:
  // Starts the task. Call once, after the I2S port exists (i.e. after the
  // playback driver's begin()).
  void begin();

  // games::BlipPlayer -- callable from any task.
  void blip(uint16_t frequencyHz, uint16_t durationMs) override;

  // Drops anything pending and stops a sounding blip, for leaving a game.
  void silence() override;

 private:
  static constexpr uint32_t kToneSampleRate = 22050;
  // How long the DAC must have been quiet before this task assumes
  // nothing is playing and starts writing itself.
  static constexpr uint32_t kStreamIdleMs = 60;
  // One buffer's worth: short enough that a decoder starting mid-blip
  // only overlaps briefly, long enough not to spin.
  static constexpr size_t kChunkFrames = 128;

  [[noreturn]] void taskLoop();
  static void taskTrampoline(void *self);

  int16_t chunk_[kChunkFrames * 2] = {};
  uint32_t lastSeenSamples_ = 0;
  uint32_t lastFlowMs_ = 0;
  bool rateIsOurs_ = false;
};

}  // namespace knobify::drivers
