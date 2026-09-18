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
#ifdef KNOBIFY_TONE_DEBUG
  triggeredMicros_ = micros();
  measuring_ = true;
#endif
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

    // Only a decoder actually producing takes the rate back. Dropping the
    // claim merely because the last blip ended made every blip reprogram
    // the I2S clock again for nothing (measured at ~0.7ms each,
    // 2026-09-18) -- and reprogramming a clock nothing has changed is the
    // kind of thing that eventually bites, not just costs.
    if (!streamIdle) rateIsOurs_ = false;

    if (!stage.tone().active() || !streamIdle) {
      vTaskDelay(pdMS_TO_TICKS(kPollMs));
      continue;
    }

#ifdef KNOBIFY_TONE_DEBUG
    if (measuring_) {
      measuring_ = false;
      Serial.printf("[tone] trigger->write %luus (rate claim %s)\n",
                    static_cast<unsigned long>(micros() - triggeredMicros_),
                    rateIsOurs_ ? "skipped" : "needed");
      rateClaimStart_ = micros();
    }
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
#ifdef KNOBIFY_TONE_DEBUG
    if (rateClaimStart_ != 0) {
      Serial.printf("[tone] rate claim took %luus\n",
                    static_cast<unsigned long>(micros() - rateClaimStart_));
      rateClaimStart_ = 0;
    }
#endif
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
