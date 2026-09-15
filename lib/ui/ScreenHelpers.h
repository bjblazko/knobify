#pragma once

#include <lvgl.h>

#include "EdgeArc.h"
#include "St77916Driver.h"

// Small LVGL helpers shared by ScreenManager's translation units
// (ScreenManager.cpp, ScreenManagerMenu.cpp).
namespace knobify::ui {

// Sets a label's text, wrapping to at most `maxLines` lines and ending
// in an ellipsis beyond that. LV_LABEL_LONG_DOT alone only truncates a
// label whose height is fixed -- with the default content height a long
// title just kept wrapping and overlapped whatever sat below it (found
// on real hardware 2026-09-13 with "70 Cities as Love Brings the Fall").
// The label's font and width must already be set.
inline void setClampedText(lv_obj_t *label, const char *text, int maxLines) {
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_label_set_text(label, text);
  lv_obj_update_layout(label);
  const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  lv_coord_t lineSpace = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
  lv_coord_t maxHeight = maxLines * lv_font_get_line_height(font) +
                         (maxLines - 1) * lineSpace;
  if (lv_obj_get_height(label) > maxHeight) {
    lv_obj_set_height(label, maxHeight);
    // Without this, lv_obj_get_height() still returns the wrapped height
    // until LVGL's next layout pass -- Now Playing placed the artist line
    // below a long title's *second* line, under the transport buttons
    // (found on the device 2026-09-15).
    lv_obj_update_layout(label);
  }
}

// Transparent, non-interactive host for an edge-hugging EdgeArc.
// Oversized beyond the screen's own bounds and let the screen object's
// default clipping (plus the physical round bezel) eat the excess -- even
// with EdgeArc's knob-padding fix, sizing the host to exactly
// kLcdHorRes/VerRes still left a visible gap from the true edge on real
// hardware (some further LVGL-internal margin), and overshooting is
// harmless here since nothing else occupies that space.
// `insetPx` shrinks the host on every side, for a ring drawn just inside
// another one (the shuttle arc inside the progress ring, ADR 0013).
inline lv_obj_t *makeEdgeArcHost(lv_obj_t *parent, lv_coord_t insetPx = 0) {
  lv_obj_t *host = lv_obj_create(parent);
  lv_obj_set_size(host, drivers::kLcdHorRes + 40 - 2 * insetPx,
                  drivers::kLcdVerRes + 40 - 2 * insetPx);
  lv_obj_center(host);
  lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(host, 0, 0);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(host, LV_OBJ_FLAG_CLICKABLE);
  return host;
}

}  // namespace knobify::ui
