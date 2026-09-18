#include "Esp32AudioI2SDriver.h"

#include <Arduino.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "AudioGain.h"
#include "AudioOutputStage.h"

namespace knobify::drivers {

void Esp32AudioI2SDriver::begin() {
  audio_.setPinout(kAudioBclkPin, kAudioLrcPin, kAudioDoutPin);
  // Input buffer 64 KB instead of the library's 300 KB PSRAM default. After
  // every seek the library discards the buffer and stays silent until it is
  // full again; measured on the device 2026-09-15, refilling 300 KB took
  // ~290 ms -- as long as the then 300 ms shuttle cue cycle (ADR 0013), so cueing
  // was mostly silence. 64 KB still holds ~1.6 s of a 320 kbps MP3, and the
  // decoder has a core to itself (ADR 0006). Must run before the first
  // connecttoFS(), which allocates the buffer.
  audio_.setBufsize(-1, kInputBufferBytes);
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

namespace knobify::drivers {

playback::SampleWindow Esp32AudioI2SDriver::readRecentSamples(int16_t *dst,
                                                              size_t maxSamples) {
  // Plain field reads, deliberately without mutex_: taking it at frame
  // rate would wait on the decode task's chunks, and a stale rate for one
  // frame right after a track change or backend switch is harmless.
  return audioOutputStage().readRecentSamples(
      dst, maxSamples, vorbisActive_ ? vorbis_.sampleRate() : audio_.getSampleRate());
}

void Esp32AudioI2SDriver::setOutputGain(uint16_t gain) {
  audioOutputStage().setOutputGain(gain);
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
using knobify::playback::AudioGain;

// ESP32-audioI2S 2.3.0's Audio::playSample() halves every sample before
// its EQ and Gain() ("half Vin so we can boost up to 6dB in filters"), so
// volume 21/21 only ever reached -6 dBFS. Doubling here, right before
// i2s_write(), makes 21 true 0 dBFS. The sleep timer's gain rides along
// via AudioGain, shared with the Vorbis path.
int16_t compensate(int16_t s, uint16_t outputGain) {
  return AudioGain::applyOutputGain(
      static_cast<int16_t>(std::clamp<int32_t>(s * 2, INT16_MIN, INT16_MAX)), outputGain);
}
}  // namespace

void audio_process_i2s(uint32_t *sample, bool *continueI2S) {
  // Packed as Gain() returns it: left in the high 16 bits, right in the low.
  // Verified on hardware 2026-09-13: loud tracks peak at 16383 in, 32766
  // out, zero clipped samples.
  auto &stage = knobify::drivers::audioOutputStage();
  const uint16_t gain = stage.outputGain();
  // Pong's blips ride on top of whatever is playing (ADR 0022). This is
  // the library path's only per-sample seam, so it is where they join;
  // when nothing is playing the hook never runs and ToneOutput.cpp pushes
  // them to the DAC itself.
  const int16_t toneSample = stage.nextToneSample();
  const int16_t left = knobify::drivers::AudioOutputStage::mixTone(
      compensate(static_cast<int16_t>(*sample >> 16), gain), toneSample);
  const int16_t right = knobify::drivers::AudioOutputStage::mixTone(
      compensate(static_cast<int16_t>(*sample & 0xFFFF), gain), toneSample);
  stage.noteMonoSample(static_cast<int16_t>((static_cast<int32_t>(left) + right) / 2));
  *sample = (static_cast<uint32_t>(static_cast<uint16_t>(left)) << 16) |
            static_cast<uint16_t>(right);
  *continueI2S = true;
}

// ESP32-audioI2S 2.3.0 calls this weak hook without checking that it exists
// when it prints AAC codec parameters (Audio::showCodecParams()), so without
// a definition the first decoded M4A frame jumped to address 0 and panicked
// (core dump, 2026-09-16). MP3 only ever calls it through a null check.
// Defined here, next to begin(), so it's always linked (see the
// audio_process_i2s note above).
void audio_info(const char *info) {
#ifdef AUDIO_LOG
  Serial.printf("[audio] %s\n", info);
#else
  (void)info;
#endif
}
