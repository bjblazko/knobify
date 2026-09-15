#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

#include "BlobStore.h"
#include "GestureRecognizer.h"
#include "TouchCalibration.h"

namespace knobify::input {

// Collects one raw tap per on-screen target and fits a TouchCalibration
// from them. Pure logic over raw CST816 samples and an explicit clock.
class TouchCalibrator {
 public:
  struct Point {
    int16_t x;
    int16_t y;
  };
  static constexpr size_t kTargetCount = 4;
  // Top, right, bottom, left -- never the corners, which the round bezel
  // hides. Three distinct values per axis, so both scale and offset fit.
  static constexpr std::array<Point, kTargetCount> kTargets = {
      Point{180, 70}, Point{290, 180}, Point{180, 290}, Point{70, 180}};
  // Ignores a press starting this soon after the capture began or the
  // previous target was taken: the tap that opened the screen, or a
  // bouncing finger, must not count as the next target.
  static constexpr uint32_t kSettleMs = 300;
  // A fitted point may miss its target by this much (in display px) --
  // generous for a fingertip, far below a tap on the wrong cross.
  static constexpr float kMaxResidualPx = 25.0f;

  void reset(uint32_t nowMs) {
    done_ = 0;
    pressing_ = false;
    settleUntilMs_ = nowMs + kSettleMs;
  }

  // Feed every raw sample. The finger's average position while down is
  // taken for the current target on release.
  void feed(const TouchSample &raw, uint32_t nowMs) {
    if (complete()) return;
    if (raw.pressed) {
      if (!pressing_) {
        if (nowMs < settleUntilMs_) return;
        pressing_ = true;
        sumX_ = sumY_ = 0;
        count_ = 0;
      }
      sumX_ += raw.x;
      sumY_ += raw.y;
      ++count_;
    } else if (pressing_) {
      pressing_ = false;
      raw_[done_] = Point{static_cast<int16_t>(sumX_ / count_),
                          static_cast<int16_t>(sumY_ / count_)};
      ++done_;
      settleUntilMs_ = nowMs + kSettleMs;
    }
  }

  size_t targetsDone() const { return done_; }
  bool complete() const { return done_ == kTargetCount; }

  // Least-squares fit per axis. Empty when the capture isn't complete, a
  // tap was far from its target (e.g. the wrong cross), or the result is
  // implausible.
  std::optional<TouchCalibration> fit() const {
    if (!complete()) return std::nullopt;
    std::array<float, kTargetCount> vx{}, rx{}, vy{}, ry{};
    for (size_t i = 0; i < kTargetCount; ++i) {
      vx[i] = kTargets[i].x;
      vy[i] = kTargets[i].y;
      rx[i] = raw_[i].x;
      ry[i] = raw_[i].y;
    }
    auto x = fitAxis(vx, rx);
    auto y = fitAxis(vy, ry);
    if (!x || !y) return std::nullopt;
    TouchCalibration cal{x->scaleMilli, x->offset, y->scaleMilli, y->offset};
    if (!cal.isPlausible()) return std::nullopt;
    return cal;
  }

 private:
  struct Axis {
    int16_t scaleMilli;
    int16_t offset;
  };

  static std::optional<Axis> fitAxis(const std::array<float, kTargetCount> &v,
                                     const std::array<float, kTargetCount> &r) {
    float n = kTargetCount, sv = 0, sr = 0, svv = 0, svr = 0;
    for (size_t i = 0; i < kTargetCount; ++i) {
      sv += v[i];
      sr += r[i];
      svv += v[i] * v[i];
      svr += v[i] * r[i];
    }
    float denom = n * svv - sv * sv;
    if (denom <= 0) return std::nullopt;
    float scale = (n * svr - sv * sr) / denom;
    float offset = (sr - scale * sv) / n;
    // Also rules out a negative slope (e.g. top and bottom swapped) before
    // it's divided by below.
    if (scale * 1000 < TouchCalibration::kMinScaleMilli ||
        scale * 1000 > TouchCalibration::kMaxScaleMilli ||
        std::fabs(offset) > TouchCalibration::kMaxAbsOffset) {
      return std::nullopt;
    }
    for (size_t i = 0; i < kTargetCount; ++i) {
      if (std::fabs((r[i] - offset) / scale - v[i]) > kMaxResidualPx) {
        return std::nullopt;
      }
    }
    return Axis{static_cast<int16_t>(std::lround(scale * 1000)),
                static_cast<int16_t>(std::lround(offset))};
  }

  std::array<Point, kTargetCount> raw_{};
  size_t done_ = 0;
  bool pressing_ = false;
  int32_t sumX_ = 0;
  int32_t sumY_ = 0;
  int32_t count_ = 0;
  uint32_t settleUntilMs_ = 0;
};

enum class CalibrationPhase { Idle, Capturing, Verifying };
enum class CalibrationOutcome { None, Saved, Reverted, Cancelled };

// The Settings > Touch calibration flow and the calibration touch input
// actually uses. A broken mapping can't be fixed by touch, so nothing is
// kept without proof it works: after a good fit the new calibration applies
// live and must be confirmed by tapping Keep (only reachable if it maps
// correctly) within kVerifyTimeoutMs, or the previous one comes back.
// cancel() -- wired to the knob, which never depends on touch -- restores
// it too.
class TouchCalibrationFlow {
 public:
  static constexpr char kKey[] = "touchcal";
  static constexpr uint32_t kVerifyTimeoutMs = 10000;

  explicit TouchCalibrationFlow(resume::BlobStore &store) : store_(store) {}

  // Loads the persisted calibration; defaults() if none or invalid.
  void begin() {
    std::vector<uint8_t> blob;
    std::optional<TouchCalibration> stored;
    if (store_.getBlob(kKey, blob)) stored = TouchCalibration::decode(blob);
    active_ = stored.value_or(TouchCalibration::defaults());
  }

  const TouchCalibration &active() const { return active_; }
  CalibrationPhase phase() const { return phase_; }
  bool isCapturing() const { return phase_ == CalibrationPhase::Capturing; }
  size_t targetsDone() const { return calibrator_.targetsDone(); }
  // True after a capture whose fit was refused, until the next one starts.
  bool lastFitRejected() const { return lastFitRejected_; }

  void start(uint32_t nowMs) {
    if (phase_ == CalibrationPhase::Idle) previous_ = active_;
    phase_ = CalibrationPhase::Capturing;
    lastFitRejected_ = false;
    calibrator_.reset(nowMs);
  }

  // Raw (uncalibrated) samples while capturing; ignored otherwise.
  void feedRaw(const TouchSample &raw, uint32_t nowMs) {
    if (phase_ != CalibrationPhase::Capturing) return;
    calibrator_.feed(raw, nowMs);
    if (!calibrator_.complete()) return;
    if (auto fitted = calibrator_.fit()) {
      active_ = *fitted;
      phase_ = CalibrationPhase::Verifying;
      verifyDeadlineMs_ = nowMs + kVerifyTimeoutMs;
    } else {
      calibrator_.reset(nowMs);
      lastFitRejected_ = true;
    }
  }

  void keep() {
    if (phase_ != CalibrationPhase::Verifying) return;
    store_.setBlob(kKey, active_.encode());
    finish(CalibrationOutcome::Saved);
  }

  void cancel() {
    if (phase_ == CalibrationPhase::Idle) return;
    active_ = previous_;
    finish(CalibrationOutcome::Cancelled);
  }

  void tick(uint32_t nowMs) {
    if (phase_ == CalibrationPhase::Verifying && nowMs >= verifyDeadlineMs_) {
      active_ = previous_;
      finish(CalibrationOutcome::Reverted);
    }
  }

  uint32_t verifyRemainingMs(uint32_t nowMs) const {
    if (phase_ != CalibrationPhase::Verifying || nowMs >= verifyDeadlineMs_) return 0;
    return verifyDeadlineMs_ - nowMs;
  }

  // How the last flow ended, reported once.
  CalibrationOutcome takeOutcome() {
    CalibrationOutcome o = outcome_;
    outcome_ = CalibrationOutcome::None;
    return o;
  }

 private:
  void finish(CalibrationOutcome outcome) {
    phase_ = CalibrationPhase::Idle;
    outcome_ = outcome;
  }

  resume::BlobStore &store_;
  TouchCalibrator calibrator_;
  TouchCalibration active_ = TouchCalibration::defaults();
  TouchCalibration previous_ = TouchCalibration::defaults();
  CalibrationPhase phase_ = CalibrationPhase::Idle;
  CalibrationOutcome outcome_ = CalibrationOutcome::None;
  bool lastFitRejected_ = false;
  uint32_t verifyDeadlineMs_ = 0;
};

}  // namespace knobify::input
