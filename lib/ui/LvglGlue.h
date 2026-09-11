#pragma once

#include <esp_lcd_panel_io.h>
#include <lvgl.h>

#include "Cst816Driver.h"
#include "St77916Driver.h"

namespace knobify::ui {

// Connects LVGL to the display and touch drivers: registers the display
// flush callback and the touch input device, and pumps lv_timer_handler().
// This is the only place LVGL's disp_drv/indev_drv registration lives --
// individual screens (ArtistsScreen etc.) just create/manipulate LVGL
// objects, they don't touch driver registration.
class LvglGlue {
 public:
  bool begin(drivers::St77916Driver &display, drivers::Cst816Driver &touch);
  void pump() { lv_timer_handler(); }

 private:
  static void flushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                       lv_color_t *colorMap);
  static void rounderCb(lv_disp_drv_t *drv, lv_area_t *area);
  static void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data);
  static bool colorTransDoneCb(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *userCtx);

  lv_disp_draw_buf_t drawBuf_{};
  lv_disp_drv_t dispDrv_{};
  lv_indev_drv_t indevDrv_{};
  drivers::Cst816Driver *touch_ = nullptr;
};

}  // namespace knobify::ui
