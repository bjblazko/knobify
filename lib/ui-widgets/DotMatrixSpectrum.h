#pragma once

#include <lvgl.h>

#include <array>
#include <cstdint>

namespace knobify::ui_widgets {

// The Now Playing spectrum (ADR 0009): a 12x12 grid of round dots filling
// the 96x96 cover slot, lit from the bottom like an LED level meter. Drawn
// into one lv_canvas by copying two pre-rendered, anti-aliased dot stamps
// (lit/unlit) -- far cheaper per frame than 144 lv_canvas_draw_rect()
// calls, each of which sets up its own draw context. Only columns whose
// height changed are re-stamped, and the canvas is only invalidated when
// something did change, so a paused track that has decayed to zero causes
// no redraws at all.
class DotMatrixSpectrum {
 public:
  static constexpr uint8_t kColumns = 12;
  static constexpr uint8_t kRows = 12;
  static constexpr lv_coord_t kCell = 8;  // 6px dot + 1px margin each side.
  static constexpr lv_coord_t kSize = kColumns * kCell;  // 96, the cover size.

  lv_obj_t *create(lv_obj_t *parent, lv_color_t lit, lv_color_t unlit,
                   lv_color_t background) {
    renderStamp(litStamp_, lit, background);
    renderStamp(unlitStamp_, unlit, background);
    canvas_ = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas_, pixels_.data(), kSize, kSize,
                         LV_IMG_CF_TRUE_COLOR);
    for (uint8_t column = 0; column < kColumns; ++column) {
      heights_[column] = 0;
      stampColumn(column, 0);
    }
    return canvas_;
  }

  // One height (0..kRows) per column, left = lowest band.
  void setLevels(const std::array<uint8_t, kColumns> &levels) {
    if (!canvas_) return;
    bool changed = false;
    for (uint8_t column = 0; column < kColumns; ++column) {
      uint8_t height = levels[column] > kRows ? kRows : levels[column];
      if (height == heights_[column]) continue;
      heights_[column] = height;
      stampColumn(column, height);
      changed = true;
    }
    if (changed) lv_obj_invalidate(canvas_);
  }

  // The canvas is owned by its screen; call when that screen goes away so
  // setLevels() stops touching a deleted object.
  void detach() { canvas_ = nullptr; }

  lv_obj_t *raw() const { return canvas_; }

 private:
  using Stamp = std::array<lv_color_t, kCell * kCell>;

  // A 6px circle centered in the 8px cell, 4x4-supersampled so the edge is
  // anti-aliased against the screen background.
  static void renderStamp(Stamp &stamp, lv_color_t dot, lv_color_t background) {
    constexpr int kSub = 4;
    constexpr float kCenter = kCell / 2.0f;
    constexpr float kRadiusSq = 3.0f * 3.0f;
    for (int y = 0; y < kCell; ++y) {
      for (int x = 0; x < kCell; ++x) {
        int inside = 0;
        for (int sy = 0; sy < kSub; ++sy) {
          for (int sx = 0; sx < kSub; ++sx) {
            float dx = x + (sx + 0.5f) / kSub - kCenter;
            float dy = y + (sy + 0.5f) / kSub - kCenter;
            if (dx * dx + dy * dy <= kRadiusSq) ++inside;
          }
        }
        auto mix = static_cast<lv_opa_t>(inside * 255 / (kSub * kSub));
        stamp[y * kCell + x] = lv_color_mix(dot, background, mix);
      }
    }
  }

  void stampColumn(uint8_t column, uint8_t height) {
    for (uint8_t row = 0; row < kRows; ++row) {
      const Stamp &stamp = row < height ? litStamp_ : unlitStamp_;
      lv_coord_t top = (kRows - 1 - row) * kCell;
      lv_coord_t left = column * kCell;
      for (lv_coord_t y = 0; y < kCell; ++y) {
        for (lv_coord_t x = 0; x < kCell; ++x) {
          pixels_[(top + y) * kSize + left + x] = stamp[y * kCell + x];
        }
      }
    }
  }

  lv_obj_t *canvas_ = nullptr;
  std::array<lv_color_t, kSize * kSize> pixels_{};
  std::array<uint8_t, kColumns> heights_{};
  Stamp litStamp_{};
  Stamp unlitStamp_{};
};

}  // namespace knobify::ui_widgets
