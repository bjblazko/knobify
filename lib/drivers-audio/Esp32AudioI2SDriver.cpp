#include "Esp32AudioI2SDriver.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdint>

namespace knobify::drivers {

void Esp32AudioI2SDriver::begin() {
  audio_.setPinout(kAudioBclkPin, kAudioLrcPin, kAudioDoutPin);
  mutex_ = xSemaphoreCreateMutex();
  // Priority 3 (above Arduino's default loopTask priority of 1) since
  // audio decoding is latency-sensitive; pinned to core 0, which
  // nothing else in this project uses (no WiFi/BT), so it never
  // contends with the main loop's LVGL/input/navigation work on core 1
  // for CPU time at all, only briefly for the mutex.
  xTaskCreatePinnedToCore(&Esp32AudioI2SDriver::audioTaskTrampoline, "audio",
                          8192, this, /*priority=*/3, &taskHandle_,
                          /*core=*/0);
}

}  // namespace knobify::drivers

// ESP32-audioI2S 2.3.0's Audio::playSample() unconditionally halves every
// sample (`sample >> 1`, "half Vin so we can boost up to 6dB in filters")
// before its EQ and Gain(), so even volume 21/21 only ever reached -6 dBFS
// -- half the PCM5100A's output voltage went unused. This per-sample hook
// runs after Gain(), right before i2s_write(), and undoes that halving:
// volume 21 becomes true 0 dBFS. Unlike the per-buffer
// audio_process_extern hook (tried and reverted, see AGENTS.md), it only
// rewrites one packed sample and leaves the library's write path intact.
namespace {
constexpr int32_t kHeadroomCompensation = 2;

int16_t compensate(int16_t s) {
  // Clamp is a safety net only: the input was halved, so x2 can't clip
  // unless the library's EQ (setTone) is ever used to boost.
  return static_cast<int16_t>(
      std::clamp<int32_t>(s * kHeadroomCompensation, INT16_MIN, INT16_MAX));
}
}  // namespace

void audio_process_i2s(uint32_t *sample, bool *continueI2S) {
  // Packed as Gain() returns it: left in the high 16 bits, right in the low.
  // Verified on hardware 2026-09-13: loud tracks peak at 16383 in, 32766
  // out, zero clipped samples.
  const int16_t left = compensate(static_cast<int16_t>(*sample >> 16));
  const int16_t right = compensate(static_cast<int16_t>(*sample & 0xFFFF));
  *sample = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
  *continueI2S = true;
}
