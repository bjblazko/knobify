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

  // Starts a square-wave tone. Any thread; the next sample the audio side
  // asks for begins it. A trigger during a tone replaces it rather than
  // layering, which is what a rapid paddle-then-wall pair should sound
  // like. durationMs == 0 holds it until silence().
  void trigger(uint16_t frequencyHz, uint16_t durationMs,
               int16_t level = kAmplitude) {
    start(frequencyHz, durationMs, level, /*noise=*/false);
  }

  // Starts noise instead of a tone (ADR 0023): Gravity's engine is a
  // rumble, and a square wave is a beep however low you pitch it.
  // clockHz is how fast the shift register is stepped, which is what
  // makes it a hiss or a roar. durationMs == 0 holds it until silence().
  void triggerNoise(uint16_t clockHz, uint16_t durationMs,
                    int16_t level = kAmplitude / 2) {
    start(clockHz, durationMs, level, /*noise=*/true);
  }

  // True while a tone is pending or sounding -- what the idle writer uses
  // to decide whether it has anything to push to the DAC.
  bool active() const {
    return request_.load(std::memory_order_relaxed) != 0 || remaining_ > 0 ||
           sustained_;
  }

  // Stops immediately and drops anything pending: a held engine note, or
  // a game being left.
  void silence() {
    request_.store(0, std::memory_order_relaxed);
    remaining_ = 0;
    sustained_ = false;
  }

  // The next mono sample at this rate, or 0 when silent. Audio side only:
  // it owns every member below the atomics.
  int16_t nextSample(uint32_t sampleRate) {
    const uint32_t request = request_.exchange(0, std::memory_order_acquire);
    if (request != 0 && sampleRate != 0) {
      const uint32_t rateHz = request >> 16;
      const uint32_t durationMs = request & 0xFFFF;
      // A phase accumulator in 16.16, not a whole number of samples per
      // half period: that truncation put a 1400 Hz tone out at 1575 Hz.
      // It happens to come out exact at 441 Hz, which is why it survived
      // Table Tennis and only showed up once a test asked for a pitch
      // that did not divide evenly (2026-09-18).
      increment_ = static_cast<uint32_t>(
          (static_cast<uint64_t>(rateHz) * 2 * kPhaseOne) / sampleRate);
      if (increment_ == 0) increment_ = 1;
      sustained_ = durationMs == 0;
      remaining_ = sustained_ ? 1 : sampleRate * durationMs / 1000;
      amplitude_ = level_.load(std::memory_order_relaxed);
      isNoise_ = noise_.load(std::memory_order_relaxed);
      phase_ = 0;
      high_ = true;
      lfsr_ = kLfsrSeed;
    }
    if (remaining_ == 0) return 0;
    if (!sustained_) --remaining_;

    const int16_t sample = high_ ? amplitude_ : static_cast<int16_t>(-amplitude_);
    phase_ += increment_;
    while (phase_ >= kPhaseOne) {
      phase_ -= kPhaseOne;
      if (isNoise_) {
        // A 16-bit maximal LFSR: the cheapest thing that sounds like a
        // rocket and nothing like a beep.
        lfsr_ = static_cast<uint16_t>((lfsr_ >> 1) ^
                                      (-(lfsr_ & 1u) & kLfsrTaps));
        high_ = (lfsr_ & 1u) != 0;
      } else {
        high_ = !high_;
      }
    }
    return sample;
  }

 private:
  static constexpr uint32_t kPhaseOne = 65536;
  static constexpr uint16_t kLfsrSeed = 0xACE1u;
  static constexpr uint16_t kLfsrTaps = 0xB400u;

  // level and noise are written before the request and read only once the
  // request has been seen, so the one release/acquire pair covers all
  // three.
  void start(uint16_t rateHz, uint16_t durationMs, int16_t level, bool noise) {
    if (rateHz == 0) return;
    level_.store(level, std::memory_order_relaxed);
    noise_.store(noise, std::memory_order_relaxed);
    request_.store((static_cast<uint32_t>(rateHz) << 16) | durationMs,
                   std::memory_order_release);
  }

  std::atomic<uint32_t> request_{0};
  std::atomic<int16_t> level_{kAmplitude};
  std::atomic<bool> noise_{false};
  uint32_t remaining_ = 0;
  uint32_t increment_ = 1;
  uint32_t phase_ = 0;
  int16_t amplitude_ = kAmplitude;
  bool high_ = true;
  bool isNoise_ = false;
  bool sustained_ = false;
  uint16_t lfsr_ = kLfsrSeed;
};

}  // namespace knobify::playback
