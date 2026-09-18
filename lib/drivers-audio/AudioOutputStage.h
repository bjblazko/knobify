#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AudioGain.h"
#include "PlaybackDriver.h"
#include "ToneGenerator.h"

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

  // The tone generator's path (ADR 0024): straight to the DAC with no
  // volume and no output gain, because its level is stated in dBFS and
  // must mean exactly that. Still recorded, so the scope sees it.
  // Blocks like writeFrames(); false on an I2S error.
  bool writeFramesUnscaled(const int16_t *interleaved, size_t frames);

  // Samples kept for whoever reads them back: the Now Playing spectrum
  // takes its 1024, the tone generator's scope up to all of them -- two
  // periods of 20 Hz at 48 kHz need 4800, and 4096 shows most of that
  // (ADR 0024). Power of two.
  static constexpr size_t kSampleRingSize = 4096;

  playback::SampleWindow readRecentSamples(int16_t *dst, size_t maxSamples,
                                           uint32_t sampleRate);

  // A game's blips (ADR 0022). They are mixed in here rather than played as
  // a file because this is the one point both decode paths pass through,
  // so a blip sounds the same whatever is playing -- and can sound *over*
  // whatever is playing, which a second player could not.
  playback::ToneGenerator &tone() { return tone_; }

  // The rate the DAC is currently clocked at, so a blip comes out at the
  // pitch it asked for whatever the track's rate is. Kept here because
  // the library path's per-sample hook is a free function with no other
  // way to find out; the driver's audio task stores it.
  void setSampleRate(uint32_t rate) {
    if (rate != 0) sampleRate_.store(rate, std::memory_order_relaxed);
  }
  uint32_t sampleRate() const { return sampleRate_.load(std::memory_order_relaxed); }

  // One tone sample at the current rate, already carrying the volume, for
  // the library path's hook to add to its own. Audio side only.
  int16_t nextToneSample() {
    const int16_t raw = tone_.nextSample(sampleRate());
    if (raw == 0) return 0;
    return playback::AudioGain::applyVolume(raw, volumeStep(), outputGain());
  }

  // Adds a blip to a sample that already carries the volume, clipping
  // rather than ducking the music: a blip is 30ms of square wave, and the
  // original clips too.
  static int16_t mixTone(int16_t sample, int16_t toneSample) {
    if (toneSample == 0) return sample;
    const int32_t sum = static_cast<int32_t>(sample) + toneSample;
    return static_cast<int16_t>(sum < INT16_MIN   ? INT16_MIN
                                : sum > INT16_MAX ? INT16_MAX
                                                  : sum);
  }

  // How many samples have reached the DAC. The idle blip writer watches
  // this to tell whether anything is playing (ToneOutput.cpp).
  uint32_t samplesWritten() const {
    return samplesWritten_.load(std::memory_order_relaxed);
  }

 private:
  static constexpr size_t kWriteChunkFrames = 256;

  int16_t ring_[kSampleRingSize] = {};
  std::atomic<uint32_t> samplesWritten_{0};
  std::atomic<uint16_t> outputGain_{4096};
  std::atomic<uint8_t> volumeStep_{0};
  uint32_t lastReadCount_ = 0;
  int16_t scratch_[kWriteChunkFrames * 2] = {};
  playback::ToneGenerator tone_;
  std::atomic<uint32_t> sampleRate_{44100};
};

// One instance; the weak audio_process_i2s() hook has no other way in.
AudioOutputStage &audioOutputStage();

}  // namespace knobify::drivers
