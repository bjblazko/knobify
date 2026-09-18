#pragma once

#include <lvgl.h>

#include "TextFont.h"

namespace knobify::ui {

// What the games share, and only the games (ADR 0022, ADR 0023).
//
// This is the one corner of the app that does not use lib/ui/Theme.h.
// ux-guidelines §3's "every screen is `surface`" rule is about controls:
// colour there signals function, and a screen with no controls on it has
// no function to signal. A game is a cabinet, not a front panel. The
// exception is bounded to games on purpose -- the next screen that wants
// to be dark argues its own case rather than citing these.
namespace game_style {

// White phosphor on a dark screen, the only two colours in either game.
inline lv_color_t phosphor() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t vacuum() { return lv_color_hex(0x000000); }

// The ground in Gravity: a solid mass under the white ridge line, so the
// landscape reads as somewhere to land rather than as a wireframe. Light
// enough that text laid on it inverts to black legibly, dark enough that
// the ridge stays the brightest thing on the screen.
inline lv_color_t ground() { return lv_color_hex(0x808080); }

// A plain filled rectangle: every moving part of Table Tennis is one.
inline lv_obj_t *makeBlock(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t w, lv_coord_t h) {
  lv_obj_t *block = lv_obj_create(parent);
  lv_obj_set_size(block, w, h);
  lv_obj_set_pos(block, x, y);
  lv_obj_set_style_bg_color(block, phosphor(), 0);
  lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(block, 0, 0);
  lv_obj_set_style_radius(block, 0, 0);  // Nothing in either game is rounded.
  lv_obj_set_style_pad_all(block, 0, 0);
  lv_obj_clear_flag(block, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
  return block;
}

// A polyline in absolute screen coordinates. Gravity is drawn entirely
// out of these: LVGL 8 rotates only images, so a craft that turns cannot
// be an lv_obj -- and line art is what a 1979 vector cabinet drew anyway.
// The caller owns the points and keeps them alive.
inline lv_obj_t *makeLine(lv_obj_t *parent, lv_coord_t width) {
  lv_obj_t *line = lv_line_create(parent);
  lv_obj_set_style_line_color(line, phosphor(), 0);
  lv_obj_set_style_line_width(line, width, 0);
  lv_obj_set_style_line_rounded(line, false, 0);
  lv_obj_set_pos(line, 0, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  return line;
}

// A short line of white text, centred at a given top edge.
inline lv_obj_t *makeLabel(lv_obj_t *parent, lv_coord_t left, lv_coord_t top,
                           lv_coord_t width, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, phosphor(), 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_pos(label, left, top);
  return label;
}

}  // namespace game_style
}  // namespace knobify::ui
