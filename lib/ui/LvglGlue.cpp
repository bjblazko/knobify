#include "LvglGlue.h"

#include <esp_heap_caps.h>

namespace knobify::ui {

namespace {
constexpr int kBufHeight = drivers::kLcdVerRes / 10;
}

bool LvglGlue::begin(drivers::St77916Driver &display,
                      drivers::Cst816Driver &touch) {
  touch_ = &touch;

  if (!display.begin(&LvglGlue::colorTransDoneCb, &dispDrv_)) {
    return false;
  }

  lv_init();

  auto *buf1 = static_cast<lv_color_t *>(heap_caps_malloc(
      drivers::kLcdHorRes * kBufHeight * sizeof(lv_color_t), MALLOC_CAP_DMA));
  auto *buf2 = static_cast<lv_color_t *>(heap_caps_malloc(
      drivers::kLcdHorRes * kBufHeight * sizeof(lv_color_t), MALLOC_CAP_DMA));
  if (!buf1 || !buf2) return false;
  lv_disp_draw_buf_init(&drawBuf_, buf1, buf2,
                         drivers::kLcdHorRes * kBufHeight);

  lv_disp_drv_init(&dispDrv_);
  dispDrv_.hor_res = drivers::kLcdHorRes;
  dispDrv_.ver_res = drivers::kLcdVerRes;
  dispDrv_.flush_cb = &LvglGlue::flushCb;
  dispDrv_.rounder_cb = &LvglGlue::rounderCb;
  dispDrv_.draw_buf = &drawBuf_;
  dispDrv_.user_data = display.panelHandle();
  lv_disp_t *disp = lv_disp_drv_register(&dispDrv_);

  lv_indev_drv_init(&indevDrv_);
  indevDrv_.type = LV_INDEV_TYPE_POINTER;
  indevDrv_.disp = disp;
  indevDrv_.read_cb = &LvglGlue::touchReadCb;
  indevDrv_.user_data = this;
  lv_indev_drv_register(&indevDrv_);

  return true;
}

void LvglGlue::flushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                        lv_color_t *colorMap) {
  auto panelHandle = static_cast<esp_lcd_panel_handle_t>(drv->user_data);
  esp_lcd_panel_draw_bitmap(panelHandle, area->x1, area->y1, area->x2 + 1,
                             area->y2 + 1, colorMap);
}

void LvglGlue::rounderCb(lv_disp_drv_t *, lv_area_t *area) {
  // The QSPI panel needs even coordinate boundaries -- ported from
  // Waveshare's demo (example_lvgl_rounder_cb).
  area->x1 = (area->x1 >> 1) << 1;
  area->y1 = (area->y1 >> 1) << 1;
  area->x2 = ((area->x2 >> 1) << 1) + 1;
  area->y2 = ((area->y2 >> 1) << 1) + 1;
}

void LvglGlue::touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  auto *self = static_cast<LvglGlue *>(drv->user_data);
  input::TouchSample sample{};
  self->touch_->poll(sample);
  data->point.x = sample.x;
  data->point.y = sample.y;
  data->state = sample.pressed ? LV_INDEV_STATE_PRESSED
                                : LV_INDEV_STATE_RELEASED;
}

bool LvglGlue::colorTransDoneCb(esp_lcd_panel_io_handle_t,
                                 esp_lcd_panel_io_event_data_t *,
                                 void *userCtx) {
  auto *drv = static_cast<lv_disp_drv_t *>(userCtx);
  lv_disp_flush_ready(drv);
  return false;
}

}  // namespace knobify::ui
