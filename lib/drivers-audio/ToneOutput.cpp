#include "ToneOutput.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "AudioOutputStage.h"

namespace knobify::drivers {

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
// Same core and stack shape as VorbisBackend's decode task; this one does
// far less, but it lives in the same "produces audio" family.
constexpr uint32_t kTaskStackBytes = 4096;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kAudioCore = 0;
}  // namespace

void ToneOutput::begin() {
  xTaskCreatePinnedToCore(&ToneOutput::taskTrampoline, "blip", kTaskStackBytes,
                          this, kTaskPriority, nullptr, kAudioCore);
}

void ToneOutput::blip(uint16_t frequencyHz, uint16_t durationMs) {
  audioOutputStage().tone().trigger(frequencyHz, durationMs);
}

void ToneOutput::silence() { audioOutputStage().tone().silence(); }

void ToneOutput::taskTrampoline(void *self) {
  static_cast<ToneOutput *>(self)->taskLoop();
}

void ToneOutput::taskLoop() {
  auto &stage = audioOutputStage();
  for (;;) {
    const uint32_t written = stage.samplesWritten();
    if (written != lastSeenSamples_) {
      lastSeenSamples_ = written;
      lastFlowMs_ = millis();
    }
    const bool streamIdle = millis() - lastFlowMs_ > kStreamIdleMs;

    if (!stage.tone().active() || !streamIdle) {
      // A decoder took over (or there is nothing to play): hand the rate
      // back, so the next track is not clocked at the blip rate.
      rateIsOurs_ = false;
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

#ifdef KNOBIFY_TONE_DEBUG
    Serial.printf("[tone] idle write: rate=%u vol=%u gain=%u written=%u\n",
                  static_cast<unsigned>(stage.sampleRate()),
                  static_cast<unsigned>(stage.volumeStep()),
                  static_cast<unsigned>(stage.outputGain()),
                  static_cast<unsigned>(written));
#endif
    if (!rateIsOurs_) {
      // The port's rate is whatever the last track set. Claim it for the
      // duration of this blip; a decoder starting up sets its own again.
      i2s_set_sample_rates(kI2sPort, kToneSampleRate);
      stage.setSampleRate(kToneSampleRate);
      rateIsOurs_ = true;
    }

    // writeFrames() mixes the tone in itself, so the music input is
    // silence and what reaches the DAC is the blip alone.
    for (size_t i = 0; i < kChunkFrames * 2; ++i) chunk_[i] = 0;
    const bool ok = stage.writeFrames(chunk_, kChunkFrames);
#ifdef KNOBIFY_TONE_DEBUG
    if (!ok) Serial.println("[tone] i2s write FAILED");
#else
    (void)ok;
#endif
    // Our own writes must not look like a decoder waking up.
    lastSeenSamples_ = stage.samplesWritten();
  }
}

}  // namespace knobify::drivers
