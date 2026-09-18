#pragma once

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace knobify::ui_widgets {

// An oscilloscope trace (ADR 0024): one lv_line over a zero line. A line
// rather than a canvas, for the reason ADR 0022 gives -- LVGL redraws only
// the line's area, where a canvas would re-blit its whole buffer.
// lv_line keeps a pointer to the points, so they live here.
class ScopeTrace {
 public:
  static constexpr size_t kPoints = 160;

  void create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t width,
              lv_coord_t height, lv_color_t traceColor, lv_color_t zeroColor) {
    width_ = width;
    height_ = height;
    zeroPoints_ = {{{0, static_cast<lv_coord_t>(height / 2)},
                    {static_cast<lv_coord_t>(width - 1),
                     static_cast<lv_coord_t>(height / 2)}}};
    zero_ = makeLine(parent, x, y, 1, zeroColor);
    lv_line_set_points(zero_, zeroPoints_.data(), 2);
    line_ = makeLine(parent, x, y, 2, traceColor);
    clear();
  }

  // `values` holds kPoints samples; `fullScale` is the value drawn at the
  // band's top edge.
  void setSamples(const int16_t *values, size_t count, int32_t fullScale) {
    if (!line_ || count < 2 || fullScale <= 0) return;
    const size_t n = std::min(count, kPoints);
    const int32_t mid = height_ / 2;
    const int32_t half = height_ / 2 - 1;
    for (size_t i = 0; i < n; ++i) {
      const int32_t v = std::clamp<int32_t>(values[i] * half / fullScale, -half, half);
      points_[i].x = static_cast<lv_coord_t>(i * (width_ - 1) / (n - 1));
      points_[i].y = static_cast<lv_coord_t>(mid - v);
    }
    lv_line_set_points(line_, points_.data(), static_cast<uint16_t>(n));
  }

  // Flat on the zero line: nothing is sounding.
  void clear() {
    if (!line_) return;
    for (size_t i = 0; i < kPoints; ++i) {
      points_[i].x = static_cast<lv_coord_t>(i * (width_ - 1) / (kPoints - 1));
      points_[i].y = static_cast<lv_coord_t>(height_ / 2);
    }
    lv_line_set_points(line_, points_.data(), kPoints);
  }

  // The screen that owned the objects is being deleted.
  void detach() {
    line_ = nullptr;
    zero_ = nullptr;
  }

 private:
  static lv_obj_t *makeLine(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t lineWidth, lv_color_t color) {
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_style_line_width(line, lineWidth, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  }

  lv_obj_t *line_ = nullptr;
  lv_obj_t *zero_ = nullptr;
  lv_coord_t width_ = 0;
  lv_coord_t height_ = 0;
  std::array<lv_point_t, kPoints> points_{};
  std::array<lv_point_t, 2> zeroPoints_{};
};

}  // namespace knobify::ui_widgets
