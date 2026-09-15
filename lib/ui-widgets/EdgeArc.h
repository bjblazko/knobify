#pragma once

#include <lvgl.h>

namespace knobify::ui_widgets {

// Thin, reusable wrapper around LVGL's native lv_arc, styled to hug an
// arc of the round display's bezel edge rather than sit as a
// corner/rectangle widget. Introduced for two call sites (Now Playing's
// volume indicator, LockOverlay's unlock-hold progress) that both need
// "show a 0..range value as a ring along the edge" -- see ADR 0005.
// Deliberately just a config+create/setValue pair, not a general
// framework: extend it if a third use case needs something this doesn't
// support yet, rather than speculatively broadening it now.
struct EdgeArcConfig {
  // Degrees, LVGL's convention (0 = 3 o'clock, clockwise).
  int16_t startAngle = 135;
  int16_t endAngle = 45;
  // How far in from the widget's own bounding box the arc sits -- use a
  // parent sized/positioned so the arc's radius lands just inside the
  // round bezel.
  uint16_t widthPx = 10;
  lv_color_t color = lv_color_white();
  lv_color_t backgroundColor;
  bool hasBackgroundColor = false;
  // LV_ARC_MODE_SYMMETRICAL fills from the middle of the range outwards --
  // the shuttle arc growing either way from the top (ADR 0013).
  lv_arc_mode_t mode = LV_ARC_MODE_NORMAL;
};

class EdgeArc {
 public:
  // `parent` should be sized/positioned so the arc's default radius (it
  // fills `parent`'s box) lands just inside the round bezel -- callers
  // size the parent object rather than this class taking a radius
  // directly, matching how other screen widgets already size themselves
  // against drivers::kLcdHorRes/kLcdVerRes.
  void create(lv_obj_t *parent, const EdgeArcConfig &config, int32_t min,
              int32_t max) {
    arc_ = lv_arc_create(parent);
    // lv_arc_create()'s default size is a small fixed box positioned at
    // the parent's top-left, not "fill the parent" -- without this, the
    // ring rendered small and stuck in the corner instead of hugging the
    // parent's (round-safe-sized) edge. Found on real hardware
    // 2026-09-13.
    lv_obj_set_size(arc_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(arc_);
    lv_arc_set_bg_angles(arc_, config.startAngle, config.endAngle);
    lv_arc_set_mode(arc_, config.mode);
    lv_arc_set_range(arc_, min, max);
    lv_arc_set_rotation(arc_, 0);
    // Not an interactive control -- LVGL's default arc has a draggable
    // knob and reacts to clicks, neither of which applies to a read-only
    // indicator.
    lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    // The default theme reserves padding on LV_PART_MAIN sized for the
    // (now-removed) knob, which otherwise shrinks the visible ring well
    // inside the host's bounds instead of reaching its edge -- found on
    // real hardware 2026-09-13 (ring didn't reach the round bezel despite
    // the host already being sized to the full display).
    lv_obj_set_style_pad_all(arc_, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, config.widthPx, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_, config.widthPx, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, config.color, LV_PART_INDICATOR);
    if (config.hasBackgroundColor) {
      lv_obj_set_style_arc_color(arc_, config.backgroundColor, LV_PART_MAIN);
    }
  }

  void setValue(int32_t value) {
    if (arc_) lv_arc_set_value(arc_, value);
  }

  // For a pulsing "try me" hint on the unfilled track (LockOverlay) --
  // independent of the indicator's own opacity, since LV_PART_MAIN
  // (background) and LV_PART_INDICATOR are styled separately.
  void setBackgroundOpacity(lv_opa_t opa) {
    if (arc_) lv_obj_set_style_opa(arc_, opa, LV_PART_MAIN);
  }

  void setValue(float fraction01) {
    if (!arc_) return;
    int32_t min = lv_arc_get_min_value(arc_);
    int32_t max = lv_arc_get_max_value(arc_);
    float clamped = fraction01 < 0.0f ? 0.0f : (fraction01 > 1.0f ? 1.0f : fraction01);
    lv_arc_set_value(arc_, min + static_cast<int32_t>(clamped * (max - min)));
  }

  lv_obj_t *raw() const { return arc_; }

 private:
  lv_obj_t *arc_ = nullptr;
};

}  // namespace knobify::ui_widgets
