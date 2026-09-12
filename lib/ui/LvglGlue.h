#pragma once

#include <lvgl.h>

#include "GestureRecognizer.h"
#include "St77916Driver.h"

namespace knobify::ui {

// Connects LVGL to the display and touch drivers: registers the display
// flush callback and the touch input device, and pumps lv_timer_handler().
// This is the only place LVGL's disp_drv/indev_drv registration lives --
// individual screens (ArtistsScreen etc.) just create/manipulate LVGL
// objects, they don't touch driver registration.
//
// Touch is polled exactly once per loop() iteration, by the caller (see
// src/main.cpp), and fed in here via feedTouch() -- LVGL's own touch
// indev does NOT poll the driver itself. It used to: LVGL's click
// detection and this project's separate swipe-gesture detector
// (lib/input/GestureRecognizer) each called Cst816Driver::poll()
// independently, so a single physical tap could report slightly
// different coordinates to each (real capacitive touch coordinates
// jitter a little between consecutive reads), occasionally reading as
// both a legitimate tap AND a >=40px swipe -- found on real hardware
// 2026-09-12: tapping a list item would navigate forward, then
// immediately swipe-back to where it started, as one flashed screen
// transition. A single poll shared by both consumers can't disagree
// with itself.
class LvglGlue {
 public:
  bool begin(drivers::St77916Driver &display);
  void pump() { lv_timer_handler(); }
  void feedTouch(const input::TouchSample &sample) { latestTouch_ = sample; }

 private:
  static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                       lv_color_t *colorMap);
  static void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data);

  lv_disp_draw_buf_t drawBuf_{};
  lv_disp_drv_t dispDrv_{};
  lv_indev_drv_t indevDrv_{};
  input::TouchSample latestTouch_{};
};

}  // namespace knobify::ui
