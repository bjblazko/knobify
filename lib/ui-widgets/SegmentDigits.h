#pragma once

#include <array>
#include <lvgl.h>

namespace knobify::ui_widgets {

// Table Tennis's score (ADR 0022), drawn the way the 1972 original draws
// it: that
// machine has no character generator, so its score comes out of a
// seven-segment decoder as blocky bars. After the dashed net this is the
// most recognisable thing on the screen, and a proportional typeface --
// this project's Montserrat included -- does not read as the game at all.
//
// Two digits, leading zero suppressed (the original shows "0", not "00").
// Kept to what the game actually needs, per ux-guidelines §7's minimal-widget
// rule: no decimal point, no hex, no colour per digit.
class SegmentDigits {
 public:
  // Segment geometry, in pixels. The score is read across the court,
  // so the bars are heavy relative to the digit.
  static constexpr lv_coord_t kThickness = 6;
  static constexpr lv_coord_t kDigitWidth = 26;
  static constexpr lv_coord_t kDigitHeight = 46;
  static constexpr lv_coord_t kDigitGap = 10;
  static constexpr lv_coord_t kWidth = 2 * kDigitWidth + kDigitGap;

  // Creates the bars at (left, top). They start blank; call setValue().
  void create(lv_obj_t *parent, lv_coord_t left, lv_coord_t top,
              lv_color_t color) {
    for (int digit = 0; digit < kDigits; ++digit) {
      const lv_coord_t x = left + digit * (kDigitWidth + kDigitGap);
      for (int segment = 0; segment < kSegments; ++segment) {
        bars_[digit][segment] = makeBar(parent, x, top, segment, color);
      }
    }
    value_ = -1;
  }

  void setValue(int value) {
    if (value == value_) return;
    value_ = value;
    if (!bars_[0][0]) return;
    const int tens = value / 10;
    const int ones = value % 10;
    // Leading zero suppressed, and the tens digit sits where it does so
    // that a one-digit score stays put rather than jumping sideways.
    applyDigit(0, tens == 0 ? -1 : tens);
    applyDigit(1, ones);
  }

  // The score lives on the screen, which is rebuilt wholesale on every
  // navigation; call this when that happens so setValue() stops writing
  // to deleted objects.
  void detach() {
    bars_[0][0] = nullptr;
    value_ = -1;
  }

  bool attached() const { return bars_[0][0] != nullptr; }

 private:
  static constexpr int kDigits = 2;
  static constexpr int kSegments = 7;

  // Segments in the conventional a..g order: top, top-right, bottom-right,
  // bottom, bottom-left, top-left, middle.
  static constexpr uint8_t kPatterns[10] = {
      0b0111111,  // 0
      0b0000110,  // 1
      0b1011011,  // 2
      0b1001111,  // 3
      0b1100110,  // 4
      0b1101101,  // 5
      0b1111101,  // 6
      0b0000111,  // 7
      0b1111111,  // 8
      0b1101111,  // 9
  };

  static lv_obj_t *makeBar(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           int segment, lv_color_t color) {
    const lv_coord_t t = kThickness;
    const lv_coord_t w = kDigitWidth;
    const lv_coord_t h = kDigitHeight;
    const lv_coord_t mid = (h - t) / 2;
    lv_coord_t bx = 0, by = 0, bw = 0, bh = 0;
    switch (segment) {
      case 0: bx = t;     by = 0;       bw = w - 2 * t; bh = t;           break;
      case 1: bx = w - t; by = t;       bw = t;         bh = mid - t;     break;
      case 2: bx = w - t; by = mid + t; bw = t;         bh = h - mid - 2 * t; break;
      case 3: bx = t;     by = h - t;   bw = w - 2 * t; bh = t;           break;
      case 4: bx = 0;     by = mid + t; bw = t;         bh = h - mid - 2 * t; break;
      case 5: bx = 0;     by = t;       bw = t;         bh = mid - t;     break;
      default: bx = t;    by = mid;     bw = w - 2 * t; bh = t;           break;
    }
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, bw, bh);
    lv_obj_set_pos(bar, x + bx, y + by);
    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);  // Square, like the decoder's output.
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    return bar;
  }

  // digit < 0 blanks it (the suppressed leading zero).
  void applyDigit(int index, int digit) {
    const uint8_t pattern =
        (digit >= 0 && digit <= 9) ? kPatterns[digit] : uint8_t{0};
    for (int segment = 0; segment < kSegments; ++segment) {
      lv_obj_t *bar = bars_[index][segment];
      if (!bar) continue;
      if (pattern & (1u << segment)) {
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  std::array<std::array<lv_obj_t *, kSegments>, kDigits> bars_{};
  int value_ = -1;
};

}  // namespace knobify::ui_widgets
