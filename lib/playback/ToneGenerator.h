#pragma once

#include <atomic>
#include <cstdint>

namespace knobify::playback {

// A game's blips (ADR 0022): a gated square wave, which is literally what
// the 1972 table-tennis machine produces -- its sounds are taps off the same divider
// chain that generates the video sync, switched on for a moment. No
// envelope, so the clicks at each end are part of the sound rather than
// something to smooth away.
//
// Triggered from the main loop (core 1) and consumed by whichever audio
// task is running (core 0), so the handover is one atomic word: the
// producer stores a request, the consumer claims it with an exchange.
// Same single-producer/single-consumer, lock-free shape as
// AudioOutputStage's sample ring, and for the same reason -- neither
// side may block the other.
class ToneGenerator {
 public:
  // Square waves are loud for their amplitude, and a blip has to sit on
  // top of music without drowning it: a quarter of full scale is audible
  // over a track and not startling on its own.
  static constexpr int16_t kAmplitude = 8192;

  // Starts a tone. Any thread; the next sample the audio side asks for
  // begins it. A trigger during a tone replaces it rather than layering,
  // which is what a rapid paddle-then-wall pair should sound like.
  void trigger(uint16_t frequencyHz, uint16_t durationMs) {
    if (frequencyHz == 0 || durationMs == 0) return;
    request_.store((static_cast<uint32_t>(frequencyHz) << 16) | durationMs,
                   std::memory_order_relaxed);
  }

  // True while a tone is pending or sounding -- what the idle writer uses
  // to decide whether it has anything to push to the DAC.
  bool active() const {
    return request_.load(std::memory_order_relaxed) != 0 || remaining_ > 0;
  }

  // Stops immediately and drops anything pending. For leaving the game.
  void silence() {
    request_.store(0, std::memory_order_relaxed);
    remaining_ = 0;
  }

  // The next mono sample at this rate, or 0 when silent. Audio side only:
  // it owns every member below the atomic.
  int16_t nextSample(uint32_t sampleRate) {
    const uint32_t request = request_.exchange(0, std::memory_order_relaxed);
    if (request != 0 && sampleRate != 0) {
      const uint32_t frequencyHz = request >> 16;
      const uint32_t durationMs = request & 0xFFFF;
      // Half a period, in samples: the wave flips every halfPeriod_.
      halfPeriod_ = sampleRate / (2 * frequencyHz);
      if (halfPeriod_ == 0) halfPeriod_ = 1;  // Above Nyquist; still a sound.
      remaining_ = sampleRate * durationMs / 1000;
      phase_ = 0;
      high_ = true;
    }
    if (remaining_ == 0) return 0;
    --remaining_;
    const int16_t sample = high_ ? kAmplitude : -kAmplitude;
    if (++phase_ >= halfPeriod_) {
      phase_ = 0;
      high_ = !high_;
    }
    return sample;
  }

 private:
  std::atomic<uint32_t> request_{0};
  uint32_t remaining_ = 0;
  uint32_t halfPeriod_ = 1;
  uint32_t phase_ = 0;
  bool high_ = true;
};

}  // namespace knobify::playback
