#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "GestureRecognizer.h"

namespace knobify::input {

// Maps the CST816's raw coordinates onto the 360x360 display, per axis as
// raw = scale * visual + offset. On this board the panel's X axis doesn't
// line up with the display (raw ~= 1.18 * visual - 66): uncorrected, every
// tap reported 20-50px left of the finger, so buttons narrower than that
// offset (prev, play, lock, back, scan) mostly missed. defaults() are the
// values fitted by hand from crosshair taps on hardware 2026-09-13 (Y
// matched within a few px); Settings > Touch calibration re-fits both axes
// on the device and persists the result (TouchCalibrationFlow.h).
struct TouchCalibration {
  static constexpr int16_t kMaxCoord = 359;
  // Plausible bounds for a fitted or stored calibration: well outside what
  // the panel does, tight enough to refuse a mapping that makes touch
  // unusable.
  static constexpr int16_t kMinScaleMilli = 700;
  static constexpr int16_t kMaxScaleMilli = 1500;
  static constexpr int16_t kMaxAbsOffset = 200;
  static constexpr uint8_t kBlobVersion = 1;
  static constexpr size_t kBlobSize = 9;

  int16_t xScaleMilli = 1000;
  int16_t xOffset = 0;
  int16_t yScaleMilli = 1000;
  int16_t yOffset = 0;

  static TouchCalibration defaults() { return {1183, -66, 1000, 0}; }

  TouchSample apply(const TouchSample &raw) const {
    return TouchSample{map(raw.x, xScaleMilli, xOffset),
                       map(raw.y, yScaleMilli, yOffset), raw.pressed};
  }

  bool isPlausible() const {
    return axisPlausible(xScaleMilli, xOffset) &&
           axisPlausible(yScaleMilli, yOffset);
  }

  // Version byte, then the four fields as little-endian int16.
  std::vector<uint8_t> encode() const {
    std::vector<uint8_t> out{kBlobVersion};
    for (int16_t v : {xScaleMilli, xOffset, yScaleMilli, yOffset}) {
      out.push_back(static_cast<uint8_t>(v & 0xFF));
      out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    return out;
  }

  static std::optional<TouchCalibration> decode(const std::vector<uint8_t> &blob) {
    if (blob.size() != kBlobSize || blob[0] != kBlobVersion) return std::nullopt;
    auto field = [&blob](size_t i) {
      return static_cast<int16_t>(blob[1 + 2 * i] | (blob[2 + 2 * i] << 8));
    };
    TouchCalibration cal{field(0), field(1), field(2), field(3)};
    if (!cal.isPlausible()) return std::nullopt;
    return cal;
  }

  bool operator==(const TouchCalibration &o) const {
    return xScaleMilli == o.xScaleMilli && xOffset == o.xOffset &&
           yScaleMilli == o.yScaleMilli && yOffset == o.yOffset;
  }

 private:
  static bool axisPlausible(int16_t scaleMilli, int16_t offset) {
    return scaleMilli >= kMinScaleMilli && scaleMilli <= kMaxScaleMilli &&
           offset >= -kMaxAbsOffset && offset <= kMaxAbsOffset;
  }

  static int16_t map(int16_t raw, int16_t scaleMilli, int16_t offset) {
    int v = (static_cast<int>(raw) - offset) * 1000 / scaleMilli;
    if (v < 0) return 0;
    if (v > kMaxCoord) return kMaxCoord;
    return static_cast<int16_t>(v);
  }
};

}  // namespace knobify::input
