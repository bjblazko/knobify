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
