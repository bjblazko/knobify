#pragma once

#include <cstdint>

namespace knobify::power {

enum class SleepPhase {
  Off,
  Running,
  // The last kFadeMs: the audio fades out and a touch or turn still
  // cancels.
  Fading,
  // Time's up: main.cpp puts the device into deep sleep.
  Expired,
};

// The global sleep timer (ADR 0015), set with the knob on the Sleep
// screen. Pure logic over an explicit clock, like IdleTimer; main.cpp fades
// the output with fadeGain() and enters deep sleep once it has Expired.
// Never persisted: every boot (including a wake from that deep sleep)
// starts with it off.
class SleepTimer {
 public:
  // Long enough to hear as a fade, not a cut (user feedback 2026-09-15:
  // 10 s in volume steps sounded like no fade at all).
  static constexpr uint32_t kFadeMs = 30000;
  static constexpr uint16_t kUnityGain = 4096;
#ifdef KNOBIFY_SLEEP_DEBUG
  // A 1-minute preset, to try the fade and deep sleep on the device.
  static constexpr uint32_t kPresetsMin[] = {0, 1, 15, 30, 45, 60, 90, 120};
#else
  static constexpr uint32_t kPresetsMin[] = {0, 15, 30, 45, 60, 90, 120};
#endif
  static constexpr int kPresetCount =
      static_cast<int>(sizeof(kPresetsMin) / sizeof(kPresetsMin[0]));

  // Moves `delta` presets from the time left and restarts from there (or
  // turns off). A preset within kSnapMs of the time left counts as the
  // current one, so 29:59 left turns up to 45, not 30 again.
  void step(int delta, uint32_t nowMs) {
    for (; delta > 0; --delta) stepOnce(1, nowMs);
    for (; delta < 0; ++delta) stepOnce(-1, nowMs);
  }

  void cancel() { durationMs_ = 0; }

  SleepPhase tick(uint32_t nowMs) const {
    if (durationMs_ == 0) return SleepPhase::Off;
    uint32_t left = remainingMs(nowMs);
    if (left == 0) return SleepPhase::Expired;
    return left <= kFadeMs ? SleepPhase::Fading : SleepPhase::Running;
  }

  bool isActive() const { return durationMs_ != 0; }

  uint32_t remainingMs(uint32_t nowMs) const {
    if (durationMs_ == 0) return 0;
    uint32_t elapsed = nowMs - startMs_;
    return elapsed >= durationMs_ ? 0 : durationMs_ - elapsed;
  }

  uint32_t remainingMinutesCeil(uint32_t nowMs) const {
    return (remainingMs(nowMs) + kMinuteMs - 1) / kMinuteMs;
  }

  // Output gain 0..kUnityGain: full until the fade starts, then down to 0
  // at expiry along (time left)^3. Loudness follows decibels, so a linear
  // amplitude ramp sounds unchanged for most of the fade and then cuts
  // out; the cube falls about evenly (-18 dB halfway, -60 dB at 90%).
  uint16_t fadeGain(uint32_t nowMs) const {
    if (durationMs_ == 0) return kUnityGain;
    uint32_t left = remainingMs(nowMs);
    if (left >= kFadeMs) return kUnityGain;
    uint64_t x = static_cast<uint64_t>(left) * kUnityGain / kFadeMs;  // 0..4095
    return static_cast<uint16_t>(x * x * x / (uint64_t{kUnityGain} * kUnityGain));
  }

 private:
  static constexpr uint32_t kMinuteMs = 60000;
  static constexpr uint32_t kSnapMs = kMinuteMs;

  void stepOnce(int direction, uint32_t nowMs) {
    uint32_t left = remainingMs(nowMs);
    // From Off, the shortest preset is next, however short.
    uint32_t snap = durationMs_ != 0 ? kSnapMs : 0;
    uint32_t next = direction > 0 ? kPresetsMin[kPresetCount - 1] : 0;
    if (direction > 0) {
      for (uint32_t minutes : kPresetsMin) {
        if (minutes * kMinuteMs > left + snap) {
          next = minutes;
          break;
        }
      }
    } else {
      for (uint32_t minutes : kPresetsMin) {
        if (minutes * kMinuteMs + snap < left) next = minutes;
      }
    }
    durationMs_ = next * kMinuteMs;
    startMs_ = nowMs;
  }

  uint32_t startMs_ = 0;
  uint32_t durationMs_ = 0;
};

}  // namespace knobify::power
