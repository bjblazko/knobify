#pragma once

#include <lvgl.h>

namespace knobify::ui {

// Factors out the create/size/align/label/center/event-cb sequence
// repeated for every icon button across ScreenManager and LockOverlay
// (back, prev, play/pause, next, lock, unlock) -- see ADR 0005. Round-
// bezel-safe positioning is still the caller's job (pass whatever
// align/offset was already validated on hardware for that button); this
// only removes the boilerplate around it.
// `font`, when non-null, must be set on the label BEFORE its text and
// before centering -- setting it afterward left the glyph invisible
// (found on real hardware 2026-09-13, on the lock/unlock buttons: their
// custom-icon-font glyph never appeared, while LV_SYMBOL_* buttons that
// never change font after creation rendered fine). Pass nullptr to keep
// the theme's default font (e.g. for LV_SYMBOL_* glyphs).
inline lv_obj_t *makeIconButton(lv_obj_t *parent, const char *symbolOrGlyph,
                                 lv_coord_t width, lv_coord_t height,
                                 lv_align_t align, lv_coord_t xOfs,
                                 lv_coord_t yOfs, lv_event_cb_t clickCb,
                                 void *userData, const lv_font_t *font = nullptr) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, width, height);
  lv_obj_align(btn, align, xOfs, yOfs);
  lv_obj_t *label = lv_label_create(btn);
  if (font) lv_obj_set_style_text_font(label, font, 0);
  lv_label_set_text(label, symbolOrGlyph);
  lv_obj_center(label);
  if (clickCb) {
    lv_obj_add_event_cb(btn, clickCb, LV_EVENT_CLICKED, userData);
  }
  return btn;
}

// For buttons whose meaning is "held" rather than "clicked" (the unlock
// button -- see LockController::onHoldStart/onHoldEnd). Wires
// PRESSED/RELEASED instead of CLICKED so a sustained hold is observable,
// not just a completed tap.
inline lv_obj_t *makeHoldButton(lv_obj_t *parent, const char *symbolOrGlyph,
                                 lv_coord_t width, lv_coord_t height,
                                 lv_align_t align, lv_coord_t xOfs,
                                 lv_coord_t yOfs, lv_event_cb_t pressCb,
                                 lv_event_cb_t releaseCb, void *userData,
                                 const lv_font_t *font = nullptr) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, width, height);
  lv_obj_align(btn, align, xOfs, yOfs);
  lv_obj_t *label = lv_label_create(btn);
  if (font) lv_obj_set_style_text_font(label, font, 0);
  lv_label_set_text(label, symbolOrGlyph);
  lv_obj_center(label);
  if (pressCb) lv_obj_add_event_cb(btn, pressCb, LV_EVENT_PRESSED, userData);
  if (releaseCb) {
    lv_obj_add_event_cb(btn, releaseCb, LV_EVENT_RELEASED, userData);
    // A press that ends outside the button's hit area (finger slides off
    // while still down) fires LOST rather than RELEASED -- treat it the
    // same as a release so a hold can't get stuck "held" forever.
    lv_obj_add_event_cb(btn, releaseCb, LV_EVENT_PRESS_LOST, userData);
  }
  return btn;
}

}  // namespace knobify::ui
