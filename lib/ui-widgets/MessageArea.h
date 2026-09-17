#pragma once

#include <lvgl.h>

#include <algorithm>
#include <cstdint>

#include "MessageTimer.h"
#include "RoundScreen.h"
#include "TextFont.h"

namespace knobify::ui_widgets {

// Where a message appears: the pill's center, in screen pixels. Each
// screen picks a spot clear of the bezel and near what triggered it.
struct MessageAnchor {
  lv_coord_t centerX;
  lv_coord_t centerY;
};

// A short text message that appears for a while, then hides -- feedback
// for a toggle ("Shuffle on - album") now, system messages later. See
// docs/design/ux-guidelines.md §7.
//
// Lives on LVGL's top layer, like BatteryIndicator: screens are rebuilt on
// every render(), which would otherwise delete a message the moment the
// tap that caused it re-renders. Looks like Now Playing's volume readout
// (ink pill, surface text) and never takes taps.
class MessageArea {
 public:
  static constexpr uint32_t kDefaultDurationMs = 2000;

  // Call once, after LVGL is initialized. Colors come from the caller's
  // theme, like DotMatrixSpectrum; `screenDiameter` bounds the pill's width.
  void begin(lv_color_t background, lv_color_t text, lv_coord_t screenDiameter) {
    screenDiameter_ = screenDiameter;
    pill_ = lv_obj_create(lv_layer_top());
    lv_obj_set_height(pill_, kHeight);
    lv_obj_set_style_radius(pill_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pill_, background, 0);
    lv_obj_set_style_border_width(pill_, 0, 0);
    lv_obj_set_style_pad_all(pill_, 0, 0);
    lv_obj_clear_flag(pill_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pill_, LV_OBJ_FLAG_CLICKABLE);

    label_ = lv_label_create(pill_);
    lv_obj_set_style_text_font(label_, &kFont, 0);
    lv_obj_set_style_text_color(label_, text, 0);
    lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_DOT);
    // One line: LONG_DOT only truncates a label with a fixed height -- with
    // content height it wraps (AGENTS.md).
    lv_obj_set_height(label_, lv_font_get_line_height(&kFont));
    lv_obj_add_flag(pill_, LV_OBJ_FLAG_HIDDEN);
  }

  // Replaces any message already showing. Too long for the round screen's
  // width at the anchor's height -> truncated with an ellipsis.
  void show(const char *text, MessageAnchor anchor, uint32_t nowMs,
            uint32_t durationMs = kDefaultDurationMs,
            messaging::MessageScope scope = messaging::MessageScope::Screen) {
    if (!pill_) return;
    lv_point_t size;
    lv_txt_get_size(&size, text, &kFont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_coord_t top = anchor.centerY - kHeight / 2;
    lv_coord_t maxWidth = static_cast<lv_coord_t>(messaging::visibleWidthAt(
        top, kHeight, screenDiameter_, kBezelMargin));
    lv_coord_t width = std::min<lv_coord_t>(size.x + 2 * kPadX, maxWidth);

    lv_obj_set_width(pill_, width);
    lv_obj_set_width(label_, std::max<lv_coord_t>(width - 2 * kPadX, 0));
    lv_label_set_text(label_, text);
    lv_obj_center(label_);
    lv_obj_set_pos(pill_, anchor.centerX - width / 2, top);
    // Above anything created on the top layer after begin().
    lv_obj_move_foreground(pill_);
    lv_obj_clear_flag(pill_, LV_OBJ_FLAG_HIDDEN);
    timer_.show(nowMs, durationMs, scope);
  }

  // Call every loop() iteration.
  void tick(uint32_t nowMs) {
    if (timer_.expire(nowMs)) lv_obj_add_flag(pill_, LV_OBJ_FLAG_HIDDEN);
  }

  // The screen changed or the device locked: a screen message no longer
  // refers to what's visible. System messages stay.
  void dismissScreenMessage() {
    if (timer_.dismissScreenMessage()) lv_obj_add_flag(pill_, LV_OBJ_FLAG_HIDDEN);
  }

  // Something else takes the spot (e.g. the volume readout).
  void hide() {
    if (!timer_.visible()) return;
    timer_.hide();
    lv_obj_add_flag(pill_, LV_OBJ_FLAG_HIDDEN);
  }

 private:
  static constexpr lv_coord_t kHeight = 36;
  static constexpr lv_coord_t kPadX = 16;
  static constexpr lv_coord_t kBezelMargin = 16;
  static constexpr const lv_font_t &kFont = knobify_text_font_16;

  lv_coord_t screenDiameter_ = 0;
  lv_obj_t *pill_ = nullptr;
  lv_obj_t *label_ = nullptr;
  messaging::MessageTimer timer_;
};

}  // namespace knobify::ui_widgets
