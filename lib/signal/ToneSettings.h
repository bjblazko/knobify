#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "KeyValueStore.h"
#include "Waveform.h"

namespace knobify::signal {

// What the knob can be set to adjust on the tone generator (ADR 0024) --
// one touch chip each.
enum class ToneParam : uint8_t { Waveform, Frequency, Level, Shape };
constexpr int kToneParamCount = 4;

// The tone generator screen's model: the values in the units a person
// turns (grid steps, whole dB, percent), which chip the knob is on, how
// fast turning moves it, what each value reads as, and persistence.
// Pure logic over an explicit clock, like BrightnessSetting.
class ToneSettings {
 public:
  // 1/48 octave per slow detent, anchored at 1 kHz so the default is
  // exact. The ends are the last steps inside 20 Hz .. 20 kHz.
  static constexpr int kStepsPerOctave = 48;
  static constexpr int kMinFrequencyStep = -270;  // 20.26 Hz
  static constexpr int kMaxFrequencyStep = 207;   // 19.87 kHz
  static constexpr int kMinLevelDb = -60;
  static constexpr int kMaxLevelDb = 0;
  // Loud enough to hear, quiet enough for headphones on first use.
  static constexpr int kDefaultLevelDb = -20;
  static constexpr int kMinDutyPercent = 5;
  static constexpr int kMaxDutyPercent = 95;

  // Knob speed tiers, in detents per second. A gap longer than
  // kSpeedWindowMs always counts as slow. Tuned on the device.
  static constexpr uint32_t kMidDetentsPerSecond = 10;
  static constexpr uint32_t kFastDetentsPerSecond = 25;
  static constexpr uint32_t kSpeedWindowMs = 250;

  static constexpr char kWaveKey[] = "tgWave";
  static constexpr char kFreqHiKey[] = "tgFreqHi";
  static constexpr char kFreqLoKey[] = "tgFreqLo";
  static constexpr char kLevelKey[] = "tgLevel";
  static constexpr char kDutyKey[] = "tgDuty";
  static constexpr char kSymmetryKey[] = "tgSym";
  static constexpr char kNoiseKey[] = "tgNoise";

  // The colour chip turns darker to brighter: turning right makes the
  // noise hiss more, as it does the pitch.
  static constexpr NoiseColor kNoiseOrder[kNoiseColorCount] = {
      NoiseColor::Brown, NoiseColor::Pink, NoiseColor::White, NoiseColor::Blue,
      NoiseColor::Violet};

  Waveform waveform() const { return waveform_; }
  int frequencyStep() const { return frequencyStep_; }
  float frequencyHz() const {
    return 1000.0f * std::exp2(static_cast<float>(frequencyStep_) / kStepsPerOctave);
  }
  int levelDb() const { return levelDb_; }
  int dutyPercent() const { return dutyPercent_; }
  // 0 = rising saw, 50 = triangle, 100 = falling saw.
  int symmetryPercent() const { return symmetryPercent_; }
  NoiseColor noiseColor() const { return kNoiseOrder[noiseIndex_]; }

  // A chip is shown only where it does something (ux-guidelines §7).
  static bool visible(ToneParam param, Waveform wave) {
    switch (param) {
      case ToneParam::Frequency:
        return wave != Waveform::Noise;
      case ToneParam::Shape:
        // Duty, symmetry or colour; a sine has no shape to set.
        return wave != Waveform::Sine;
      default:
        return true;
    }
  }

  // Never a chip that is not shown: Level, which every waveform has, stands
  // in for one that is not.
  ToneParam selected() const {
    return visible(selected_, waveform_) ? selected_ : ToneParam::Level;
  }
  void select(ToneParam param) {
    if (visible(param, waveform_)) selected_ = param;
  }

  // Adjusts the selected value; returns whether anything changed.
  bool turn(int delta, uint32_t nowMs) {
    if (delta == 0) return false;
    const ToneParam param = selected();
    const int step = delta * multiplier(param, speedTier(delta, nowMs));
    switch (param) {
      case ToneParam::Waveform: {
        const int next = std::clamp(static_cast<int>(waveform_) + delta, 0,
                                    kWaveformCount - 1);
        return set(waveform_, static_cast<Waveform>(next));
      }
      case ToneParam::Frequency:
        return set(frequencyStep_, std::clamp(frequencyStep_ + step,
                                              kMinFrequencyStep, kMaxFrequencyStep));
      case ToneParam::Level:
        return set(levelDb_, std::clamp(levelDb_ + step, kMinLevelDb, kMaxLevelDb));
      case ToneParam::Shape:
        if (waveform_ == Waveform::Noise) {
          // One colour a detent however fast, like the waveform.
          return set(noiseIndex_, std::clamp(noiseIndex_ + delta, 0, kNoiseColorCount - 1));
        }
        if (waveform_ == Waveform::Square) {
          return set(dutyPercent_, std::clamp(dutyPercent_ + step, kMinDutyPercent,
                                              kMaxDutyPercent));
        }
        return set(symmetryPercent_, std::clamp(symmetryPercent_ + step, 0, 100));
    }
    return false;
  }

  OscillatorParams params() const {
    OscillatorParams p;
    p.waveform = waveform_;
    p.frequencyHz = frequencyHz();
    p.amplitude = dbToLinear(levelDb_);
    p.noise = noiseColor();
    if (waveform_ == Waveform::Square) {
      p.shape = dutyPercent_ / 100.0f;
    } else if (waveform_ == Waveform::Saw) {
      // The oscillator wants the share of the period spent rising.
      p.shape = 1.0f - symmetryPercent_ / 100.0f;
    }
    return p;
  }

  static float dbToLinear(int db) {
    return std::pow(10.0f, static_cast<float>(db) / 20.0f);
  }

  static const char *waveformName(Waveform wave) {
    switch (wave) {
      case Waveform::Sine: return "Sine";
      case Waveform::Square: return "Square";
      case Waveform::Saw: return "Saw";
      case Waveform::Noise: return "Noise";
    }
    return "";
  }

  static const char *noiseColorName(NoiseColor color) {
    switch (color) {
      case NoiseColor::White: return "White";
      case NoiseColor::Pink: return "Pink";
      case NoiseColor::Brown: return "Brown";
      case NoiseColor::Blue: return "Blue";
      case NoiseColor::Violet: return "Violet";
    }
    return "";
  }

  static const char *chipLabel(ToneParam param, Waveform wave) {
    switch (param) {
      case ToneParam::Waveform: return waveformName(wave);
      case ToneParam::Frequency: return "Hz";
      case ToneParam::Level: return "dB";
      case ToneParam::Shape:
        if (wave == Waveform::Noise) return "Color";
        return wave == Waveform::Square ? "Duty" : "Shape";
    }
    return "";
  }

  void valueText(ToneParam param, char *out, size_t size) const {
    switch (param) {
      case ToneParam::Waveform:
        snprintf(out, size, "%s", waveformName(waveform_));
        return;
      case ToneParam::Frequency: {
        const float hz = frequencyHz();
        if (hz < 100.0f) {
          snprintf(out, size, "%.1f Hz", hz);
        } else if (hz < 1000.0f) {
          snprintf(out, size, "%.0f Hz", hz);
        } else if (hz < 10000.0f) {
          snprintf(out, size, "%.2f kHz", hz / 1000.0f);
        } else {
          snprintf(out, size, "%.1f kHz", hz / 1000.0f);
        }
        return;
      }
      case ToneParam::Level:
        snprintf(out, size, "%d dB", levelDb_);
        return;
      case ToneParam::Shape:
        if (waveform_ == Waveform::Noise) {
          snprintf(out, size, "%s noise", noiseColorName(noiseColor()));
        } else if (waveform_ == Waveform::Square) {
          snprintf(out, size, "Duty %d%%", dutyPercent_);
        } else if (symmetryPercent_ == 0) {
          snprintf(out, size, "Rising");
        } else if (symmetryPercent_ == 50) {
          snprintf(out, size, "Triangle");
        } else if (symmetryPercent_ == 100) {
          snprintf(out, size, "Falling");
        } else {
          snprintf(out, size, "Shape %d%%", symmetryPercent_);
        }
        return;
    }
  }

  // Anything missing or out of range keeps its default rather than being
  // trusted.
  void load(playback::KeyValueStore &store) {
    uint8_t v = 0;
    if (store.getU8(kWaveKey, v) && v < kWaveformCount) {
      waveform_ = static_cast<Waveform>(v);
    }
    uint8_t hi = 0;
    uint8_t lo = 0;
    if (store.getU8(kFreqHiKey, hi) && store.getU8(kFreqLoKey, lo)) {
      const int step = ((hi << 8) | lo) + kMinFrequencyStep;
      if (step >= kMinFrequencyStep && step <= kMaxFrequencyStep) frequencyStep_ = step;
    }
    if (store.getU8(kLevelKey, v) && v <= -kMinLevelDb) levelDb_ = -static_cast<int>(v);
    if (store.getU8(kDutyKey, v) && v >= kMinDutyPercent && v <= kMaxDutyPercent) {
      dutyPercent_ = v;
    }
    if (store.getU8(kSymmetryKey, v) && v <= 100) symmetryPercent_ = v;
    if (store.getU8(kNoiseKey, v) && v < kNoiseColorCount) {
      for (int i = 0; i < kNoiseColorCount; ++i) {
        if (kNoiseOrder[i] == static_cast<NoiseColor>(v)) noiseIndex_ = i;
      }
    }
  }

  void save(playback::KeyValueStore &store) const {
    const auto frequency = static_cast<uint16_t>(frequencyStep_ - kMinFrequencyStep);
    store.setU8(kWaveKey, static_cast<uint8_t>(waveform_));
    store.setU8(kFreqHiKey, static_cast<uint8_t>(frequency >> 8));
    store.setU8(kFreqLoKey, static_cast<uint8_t>(frequency & 0xFF));
    store.setU8(kLevelKey, static_cast<uint8_t>(-levelDb_));
    store.setU8(kDutyKey, static_cast<uint8_t>(dutyPercent_));
    store.setU8(kSymmetryKey, static_cast<uint8_t>(symmetryPercent_));
    store.setU8(kNoiseKey, static_cast<uint8_t>(noiseColor()));
  }

 private:
  template <typename T>
  static bool set(T &field, T value) {
    if (field == value) return false;
    field = value;
    return true;
  }

  // 0 slow, 1 mid, 2 fast.
  int speedTier(int delta, uint32_t nowMs) {
    const uint32_t since = nowMs - lastTurnMs_;
    const bool first = !turnedBefore_;
    turnedBefore_ = true;
    lastTurnMs_ = nowMs;
    if (first || since >= kSpeedWindowMs) return 0;
    const uint32_t perSecond =
        static_cast<uint32_t>(std::abs(delta)) * 1000u / std::max<uint32_t>(since, 1);
    if (perSecond >= kFastDetentsPerSecond) return 2;
    if (perSecond >= kMidDetentsPerSecond) return 1;
    return 0;
  }

  // How far one detent goes at each speed. Frequency reaches 1/3 octave;
  // a waveform is never skipped.
  static int multiplier(ToneParam param, int tier) {
    static constexpr int kTable[kToneParamCount][3] = {
        {1, 1, 1},   // Waveform
        {1, 4, 16},  // Frequency
        {1, 1, 3},   // Level
        {1, 2, 5},   // Shape
    };
    return kTable[static_cast<int>(param)][tier];
  }

  Waveform waveform_ = Waveform::Sine;
  int frequencyStep_ = 0;
  int levelDb_ = kDefaultLevelDb;
  int dutyPercent_ = 50;
  int symmetryPercent_ = 0;
  // Into kNoiseOrder; White.
  int noiseIndex_ = 2;
  ToneParam selected_ = ToneParam::Frequency;
  uint32_t lastTurnMs_ = 0;
  bool turnedBefore_ = false;
};

}  // namespace knobify::signal
