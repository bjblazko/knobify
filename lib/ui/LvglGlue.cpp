#include "LvglGlue.h"

#include <esp_heap_caps.h>

namespace knobify::ui {

namespace {
constexpr int kBufHeight = drivers::kLcdVerRes / 10;
}

bool LvglGlue::begin(drivers::St77916Driver &display,
                      drivers::Cst816Driver &touch) {
  touch_ = &touch;

  if (!display.begin()) {
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
  dispDrv_.draw_buf = &drawBuf_;
  dispDrv_.user_data = display.gfx();
  lv_disp_t *disp = lv_disp_drv_register(&dispDrv_);

  lv_indev_drv_init(&indevDrv_);
  indevDrv_.type = LV_INDEV_TYPE_POINTER;
  indevDrv_.disp = disp;
  indevDrv_.read_cb = &LvglGlue::touchReadCb;
  indevDrv_.user_data = this;
  lv_indev_drv_register(&indevDrv_);

  // A deliberate dark theme -- registering no theme explicitly still
  // gets LVGL's own built-in default (a dated, unstyled teal-on-white
  // look with poor contrast for our manual highlight overrides), which
  // is what shipped in the first on-device look at this screen
  // (2026-09-11) and read as "a 90s website". Blue accent, dark
  // background, LVGL's default font.
  lv_theme_t *theme = lv_theme_default_init(
      disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_GREY),
      /*dark_mode=*/true, LV_FONT_DEFAULT);
  lv_disp_set_theme(disp, theme);

  return true;
}

void LvglGlue::flushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                        lv_color_t *colorMap) {
  auto *gfx = static_cast<Arduino_TFT *>(drv->user_data);
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  // Arduino_GFX's generic draw16bitRGBBitmap writes one pixel at a time
  // (its own writeAddrWindow(x,y,1,1) call per pixel) -- correct but far
  // too slow for a responsive UI. Setting the address window once for
  // the whole flushed rect and bulk-pushing the pixels is the fast path
  // Arduino_TFT itself uses internally for fillRect, confirmed working
  // on this board's QSPI setup during hardware bring-up (2026-09-11).
  gfx->startWrite();
  gfx->writeAddrWindow(area->x1, area->y1, w, h);
  gfx->writePixels(reinterpret_cast<uint16_t *>(colorMap), w * h);
  gfx->endWrite();
  // This call is synchronous (blocks until the QSPI transaction
  // completes), so flush_ready is called immediately after -- no async
  // "transfer done" callback needed, unlike the esp_lcd-based approach
  // this replaced.
  lv_disp_flush_ready(drv);
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

}  // namespace knobify::ui
