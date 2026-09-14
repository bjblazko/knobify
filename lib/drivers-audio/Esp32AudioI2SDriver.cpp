#include "Esp32AudioI2SDriver.h"

#include <Arduino.h>

#include <algorithm>
#include <atomic>
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

// Spectrum analyzer tap (ADR 0009): audio_process_i2s below pushes a mono
// downmix of every sample into this ring. Single producer (the audio task,
// core 0) and single consumer (readRecentSamples(), main loop, core 1);
// the producer only ever stores a sample and then bumps the counter, so
// there's no lock in the hot path. A reader racing a writer can at worst
// see the oldest sample of its window overwritten by a newer one, which
// is invisible in a spectrum.
namespace {
constexpr size_t kSampleRingSize = 1024;  // Power of two.
int16_t g_sampleRing[kSampleRingSize];
std::atomic<uint32_t> g_samplesWritten{0};

// ESP32-audioI2S's volumetable (Audio.h): Gain() multiplies by entry/64.
// The library's own >>1 and this file's x2 compensation cancel out.
constexpr uint8_t kVolumeTable[22] = {0,  1,  2,  3,  4,  6,  8,  10,
                                      12, 14, 17, 20, 23, 27, 30, 34,
                                      38, 43, 48, 52, 58, 64};
}  // namespace

namespace knobify::drivers {

playback::SampleWindow Esp32AudioI2SDriver::readRecentSamples(
    int16_t *dst, size_t maxSamples) {
  uint32_t written = g_samplesWritten.load(std::memory_order_relaxed);
  if (written == lastSampleCount_) return {};  // Paused or between tracks.
  lastSampleCount_ = written;

  size_t count = std::min<size_t>({maxSamples, kSampleRingSize, written});
  for (size_t i = 0; i < count; ++i) {
    dst[i] = g_sampleRing[(written - count + i) & (kSampleRingSize - 1)];
  }
  playback::SampleWindow window;
  window.count = count;
  // A plain field read, deliberately without mutex_: taking it at frame
  // rate would wait on the audio task's decode chunks, and a stale rate
  // for one frame right after a track change is harmless.
  window.sampleRate = audio_.getSampleRate();
  window.gain = kVolumeTable[std::min<uint8_t>(volume_.load(), 21)] / 64.0f;
  return window;
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
  uint32_t written = g_samplesWritten.load(std::memory_order_relaxed);
  g_sampleRing[written & (kSampleRingSize - 1)] =
      static_cast<int16_t>((static_cast<int32_t>(left) + right) / 2);
  g_samplesWritten.store(written + 1, std::memory_order_relaxed);
  *sample = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
  *continueI2S = true;
}
