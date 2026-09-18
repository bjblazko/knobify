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
      // One tone sample per frame, mixed on top of the music (ADR 0022).
      const int16_t toneSample = nextToneSample();
      scratch_[i * 2] = mixTone(left, toneSample);
      scratch_[i * 2 + 1] = mixTone(right, toneSample);
      noteMonoSample(
          static_cast<int16_t>((static_cast<int32_t>(scratch_[i * 2]) +
                                scratch_[i * 2 + 1]) /
                               2));
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
