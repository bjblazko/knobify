#pragma once

#include <algorithm>
#include <cstdint>

namespace knobify::playback {

// The one place knobify decides how loud a sample is. Both decode paths
// use it (ADR 0016, ADR 0017): the ESP32-audioI2S path applies the volume
// itself inside the library and only needs the sleep timer's output gain,
// while the Vorbis path gets raw PCM and applies both.
class AudioGain {
 public:
  // Output gain is 0..4096, where 4096 is unity -- the sleep timer fades
  // with it (ADR 0015) because the 22 volume steps are far too coarse.
  static constexpr uint16_t kUnityOutputGain = 4096;
  static constexpr uint8_t kMaxVolumeStep = 21;

  // ESP32-audioI2S's own volumetable (Audio.h): Gain() multiplies by
  // entry/64. Copied so the Vorbis path sounds identical at every step;
  // a unit test keeps the copy honest.
  static constexpr uint8_t kVolumeTable[kMaxVolumeStep + 1] = {
      0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 17, 20, 23, 27, 30, 34, 38, 43, 48, 52, 58, 64};

  // For samples that already carry the volume (the library path).
  static int16_t applyOutputGain(int16_t sample, uint16_t outputGain) {
    const int32_t gain = std::min<int32_t>(outputGain, kUnityOutputGain);
    return clamp(static_cast<int32_t>(sample) * gain / kUnityOutputGain);
  }

  // For raw decoder output (the Vorbis path).
  static int16_t applyVolume(int16_t sample, uint8_t volumeStep, uint16_t outputGain) {
    const int32_t gain = std::min<int32_t>(outputGain, kUnityOutputGain);
    const int32_t step = kVolumeTable[std::min(volumeStep, kMaxVolumeStep)];
    return clamp(static_cast<int32_t>(sample) * step / 64 * gain / kUnityOutputGain);
  }

  // What the spectrum reports as the gain already applied to its samples.
  static float linearGain(uint8_t volumeStep, uint16_t outputGain) {
    const uint16_t gain = std::min<uint16_t>(outputGain, kUnityOutputGain);
    return kVolumeTable[std::min(volumeStep, kMaxVolumeStep)] / 64.0f * gain /
           kUnityOutputGain;
  }

 private:
  static int16_t clamp(int32_t value) {
    return static_cast<int16_t>(std::clamp<int32_t>(value, INT16_MIN, INT16_MAX));
  }
};

}  // namespace knobify::playback
