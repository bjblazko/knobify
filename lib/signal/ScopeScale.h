#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace knobify::signal {

// The scope's two scales (ADR 0024), stepped the way a bench scope's
// time/div and volts/div knobs are, so a change shows up as a change.
//
// A scale that always fitted the signal -- two periods wide, the set
// level tall -- drew 200 Hz and 400 Hz, or -20 dB and -26 dB, as the same
// picture. Held in steps, doubling the pitch doubles the waves on screen
// and +6 dB doubles their height, until the picture gets too crowded or
// too small and the scale moves one step (and the label says so).
//
// Both scales hold on to their step a little past the point where a fresh
// choice would differ, so a value turned back and forth across a boundary
// does not make the picture jump back and forth with it.
class ScopeScale {
 public:
  // The band's full width, 1-2-5 like a scope's timebase. The longest
  // must fit the output stage's 4096-sample ring at 48 kHz (85 ms).
  static constexpr uint32_t kTimebasesUs[] = {100,  200,   500,   1000, 2000,
                                              5000, 10000, 20000, 50000};
  static constexpr size_t kTimebaseCount =
      sizeof(kTimebasesUs) / sizeof(kTimebasesUs[0]);
  // A fresh choice shows at least this many periods; with steps no more
  // than 2.5x apart that means at most 5.
  static constexpr float kMinPeriods = 2.0f;
  static constexpr float kMaxPeriods = 5.0f;
  static constexpr float kHysteresis = 1.25f;
  static constexpr int kLevelStepDb = 10;
  // How far under the band's top a level may drop before the range steps
  // down: 12 dB is a quarter of the height, still clearly a wave.
  static constexpr int kLevelHoldDb = 12;

  void update(float frequencyHz, int levelDb) {
    updateTimebase(frequencyHz);
    updateLevel(levelDb);
  }

  uint32_t timebaseUs() const { return kTimebasesUs[timebase_]; }
  // The level, in dBFS, drawn at the band's top and bottom edges.
  int topDb() const { return topDb_; }

  // "5 ms · -20 dB": the band's width and the level at its edge.
  void label(char *out, size_t size) const {
    const uint32_t us = timebaseUs();
    if (us < 1000) {
      snprintf(out, size, "%lu \xC2\xB5s \xC2\xB7 %d dB",
               static_cast<unsigned long>(us), topDb_);
    } else {
      snprintf(out, size, "%lu ms \xC2\xB7 %d dB",
               static_cast<unsigned long>(us / 1000), topDb_);
    }
  }

 private:
  static float periodsIn(float hz, uint32_t us) {
    return hz * static_cast<float>(us) / 1000000.0f;
  }

  void updateTimebase(float hz) {
    if (hz <= 0.0f) return;
    if (hasTimebase_) {
      const float periods = periodsIn(hz, kTimebasesUs[timebase_]);
      if (periods >= kMinPeriods / kHysteresis &&
          periods <= kMaxPeriods * kHysteresis) {
        return;
      }
    }
    hasTimebase_ = true;
    timebase_ = kTimebaseCount - 1;  // A tone too low for any step.
    for (size_t i = 0; i < kTimebaseCount; ++i) {
      if (periodsIn(hz, kTimebasesUs[i]) >= kMinPeriods) {
        timebase_ = i;
        return;
      }
    }
  }

  void updateLevel(int db) {
    if (hasLevel_ && db <= topDb_ && db >= topDb_ - kLevelHoldDb) return;
    hasLevel_ = true;
    // The nearest step at or above the level.
    topDb_ = -kLevelStepDb * ((-db) / kLevelStepDb);
  }

  size_t timebase_ = 0;
  int topDb_ = 0;
  bool hasTimebase_ = false;
  bool hasLevel_ = false;
};

}  // namespace knobify::signal
