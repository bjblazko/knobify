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

void ToneOutput::noise(uint16_t clockHz, uint16_t durationMs, int16_t level) {
#ifdef KNOBIFY_TONE_DEBUG
  triggeredMicros_ = micros();
  measuring_ = true;
#endif
  audioOutputStage().tone().triggerNoise(clockHz, durationMs, level);
}

void ToneOutput::silence() { audioOutputStage().tone().silence(); }

size_t ToneOutput::readRecent(int16_t *dst, size_t maxSamples) {
  return audioOutputStage()
      .readRecentSamples(dst, maxSamples, signal::kGeneratorSampleRate)
      .count;
}

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

    // The generator keeps writing through its fade-out after stop(), so
    // the last thing the DAC hears is silence rather than a cut.
    const bool generating = control_.running() || !oscillator_.idle();
    if ((!stage.tone().active() && !generating) || !streamIdle) {
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
    const uint32_t rate = generating ? signal::kGeneratorSampleRate : kToneSampleRate;
    if (!rateIsOurs_ || claimedRate_ != rate) {
      // The port's rate is whatever the last track set. Claim it; a
      // decoder starting up, or a track resumed, sets its own again.
      i2s_set_sample_rates(kI2sPort, rate);
      stage.setSampleRate(rate);
      rateIsOurs_ = true;
      claimedRate_ = rate;
    }

#ifdef KNOBIFY_TONE_DEBUG
    if (rateClaimStart_ != 0) {
      Serial.printf("[tone] rate claim took %luus\n",
                    static_cast<unsigned long>(micros() - rateClaimStart_));
      rateClaimStart_ = 0;
    }
#endif
    bool ok = true;
    if (generating) {
      oscillator_.setParams(control_.snapshot());
      if (control_.running()) {
        oscillator_.start();
      } else {
        oscillator_.stop();
      }
      oscillator_.render(mono_, kChunkFrames, rate);
      for (size_t i = 0; i < kChunkFrames; ++i) {
        chunk_[i * 2] = mono_[i];
        chunk_[i * 2 + 1] = mono_[i];
      }
#ifdef KNOBIFY_GENERATOR_DEBUG
      logGenerator(rate);
#endif
      ok = stage.writeFramesUnscaled(chunk_, kChunkFrames);
    } else {
      // writeFrames() mixes the blip in itself, so the music input is
      // silence and what reaches the DAC is the blip alone.
      for (size_t i = 0; i < kChunkFrames * 2; ++i) chunk_[i] = 0;
      ok = stage.writeFrames(chunk_, kChunkFrames);
    }
#if defined(KNOBIFY_TONE_DEBUG) || defined(KNOBIFY_GENERATOR_DEBUG)
    if (!ok) Serial.println("[tone] i2s write FAILED");
#else
    (void)ok;
#endif
    // Our own writes must not look like a decoder waking up.
    lastSeenSamples_ = stage.samplesWritten();
  }
}

#ifdef KNOBIFY_GENERATOR_DEBUG
// Once a second: the pitch and peak of what was actually written, measured
// from the samples rather than taken from the settings -- the check that
// the grid, the rate claim and the level all agree (ADR 0024).
void ToneOutput::logGenerator(uint32_t rate) {
  for (size_t i = 0; i < kChunkFrames; ++i) {
    const int16_t s = mono_[i];
    if (debugLast_ < 0 && s >= 0) ++debugCrossings_;
    debugLast_ = s;
    const int16_t magnitude = static_cast<int16_t>(s < 0 ? -s : s);
    if (magnitude > debugPeak_) debugPeak_ = magnitude;
  }
  debugSamples_ += kChunkFrames;
  if (debugSamples_ < rate) return;
  const float seconds = static_cast<float>(debugSamples_) / rate;
  const float dbfs = debugPeak_ > 0 ? 20.0f * log10f(debugPeak_ / 32767.0f) : -99.0f;
  Serial.printf("[generator] %.1f Hz, peak %d (%.1f dBFS) at %lu Hz\n",
                debugCrossings_ / seconds, debugPeak_, dbfs,
                static_cast<unsigned long>(rate));
  debugSamples_ = 0;
  debugCrossings_ = 0;
  debugPeak_ = 0;
}
#endif

}  // namespace knobify::drivers
