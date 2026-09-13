#include "LvglGlue.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace knobify::ui {

namespace {
constexpr int kBufHeight = drivers::kLcdVerRes / 10;
}

bool LvglGlue::begin(drivers::St77916Driver &display) {
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

  gfx_ = display.gfx();
  shadowFrame_ = static_cast<uint16_t *>(
      heap_caps_malloc(drivers::kLcdHorRes * drivers::kLcdVerRes *
                            sizeof(uint16_t),
                        MALLOC_CAP_SPIRAM));

  lv_disp_drv_init(&dispDrv_);
  dispDrv_.hor_res = drivers::kLcdHorRes;
  dispDrv_.ver_res = drivers::kLcdVerRes;
  dispDrv_.flush_cb = &LvglGlue::flushCb;
  dispDrv_.draw_buf = &drawBuf_;
  dispDrv_.user_data = this;
  lv_disp_t *disp = lv_disp_drv_register(&dispDrv_);

  lv_indev_drv_init(&indevDrv_);
  indevDrv_.type = LV_INDEV_TYPE_POINTER;
  indevDrv_.disp = disp;
  indevDrv_.read_cb = &LvglGlue::touchReadCb;
  indevDrv_.user_data = this;
  lv_indev_drv_register(&indevDrv_);

  // Light theme: this is a reflective IPS LCD, not an OLED/AMOLED --
  // dark UIs (tried first) don't render as well on it. Indigo accent on
  // a light background instead of the earlier dark/blue theme, per user
  // request 2026-09-12. The theme's own light-mode contrast rules apply
  // automatically to every widget already themed via LV_STATE_CHECKED
  // etc. (see ScreenManager::applyHighlight()) -- no other UI code needs
  // to change for this.
  lv_theme_t *theme = lv_theme_default_init(
      disp, lv_palette_main(LV_PALETTE_INDIGO),
      lv_palette_main(LV_PALETTE_GREY),
      /*dark_mode=*/false, LV_FONT_DEFAULT);
  lv_disp_set_theme(disp, theme);

  return true;
}

void LvglGlue::flushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                        lv_color_t *colorMap) {
  auto *self = static_cast<LvglGlue *>(drv->user_data);
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;
  // Arduino_GFX's generic draw16bitRGBBitmap writes one pixel at a time
  // (its own writeAddrWindow(x,y,1,1) call per pixel) -- correct but far
  // too slow for a responsive UI. Setting the address window once for
  // the whole flushed rect and bulk-pushing the pixels is the fast path
  // Arduino_TFT itself uses internally for fillRect, confirmed working
  // on this board's QSPI setup during hardware bring-up (2026-09-11).
  self->gfx_->startWrite();
  self->gfx_->writeAddrWindow(area->x1, area->y1, w, h);
  self->gfx_->writePixels(reinterpret_cast<uint16_t *>(colorMap), w * h);
  self->gfx_->endWrite();

  // Mirror into the full-screen shadow buffer for writeScreenshotToSerial()
  // -- LVGL only ever flushes partial stripes (kBufHeight rows at a
  // time), so there's no single existing buffer that already holds a
  // complete frame to just read back.
  if (self->shadowFrame_) {
    const uint16_t *src = reinterpret_cast<uint16_t *>(colorMap);
    for (int32_t row = 0; row < h; ++row) {
      uint16_t *dst = self->shadowFrame_ +
                       (area->y1 + row) * drivers::kLcdHorRes + area->x1;
      memcpy(dst, src + row * w, w * sizeof(uint16_t));
    }
  }

  // This call is synchronous (blocks until the QSPI transaction
  // completes), so flush_ready is called immediately after -- no async
  // "transfer done" callback needed, unlike the esp_lcd-based approach
  // this replaced.
  lv_disp_flush_ready(drv);
}

void LvglGlue::writeScreenshotToSerial() const {
  if (!shadowFrame_) {
    Serial.println("SCREENSHOT_ERROR no shadow framebuffer");
    return;
  }
  Serial.printf("SCREENSHOT %d %d 16\n", drivers::kLcdHorRes,
                drivers::kLcdVerRes);
  Serial.write(reinterpret_cast<const uint8_t *>(shadowFrame_),
               drivers::kLcdHorRes * drivers::kLcdVerRes * sizeof(uint16_t));
  Serial.flush();
}

void LvglGlue::touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  auto *self = static_cast<LvglGlue *>(drv->user_data);
  data->point.x = self->latestTouch_.x;
  data->point.y = self->latestTouch_.y;
  data->state = self->latestTouch_.pressed ? LV_INDEV_STATE_PRESSED
                                            : LV_INDEV_STATE_RELEASED;
}

}  // namespace knobify::ui
