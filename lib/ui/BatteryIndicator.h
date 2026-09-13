#pragma once

#include <lvgl.h>

#include "BatteryMonitor.h"

namespace knobify::ui {

// Always-on battery icon, corner-anchored on LVGL's top layer
// (lv_layer_top()) -- same trick as LockOverlay (see that class's
// comment for why): objects there are drawn above whatever
// ScreenManager last rendered, with zero coordination needed from
// ScreenManager itself. Construct/begin() this AFTER LockOverlay so it
// z-orders above it too (later-created top-layer children draw on top)
// -- battery status stays visible even while the device is locked.
//
// Thin and LVGL-facing, not host-tested -- the actual percent/level
// logic lives in the host-tested power::BatteryMonitor; this class only
// renders it.
class BatteryIndicator {
 public:
  explicit BatteryIndicator(power::BatteryMonitor &monitor)
      : monitor_(monitor) {}

  // Vertical midline of ScreenManager's back/scan button (TOP_MID, y=12,
  // height 32 -- see ScreenManager.cpp's renderBackButtonIfNeeded()) --
  // matched exactly so this icon sits on the same row rather than just
  // near it.
  static constexpr int32_t kRowCenterY = 12 + 32 / 2;
  // Sideways offset from center, clear of the back/scan button (56px
  // wide, so its own right edge is at +28) and, per the round-bezel
  // clipping this project has hit repeatedly (see ScreenManager.cpp,
  // ADR 0004), nowhere near the Now Playing volume ring that hugs the
  // physical edge.
  static constexpr int32_t kOffsetX = 60;

  // Call once, after LVGL is initialized and after LockOverlay::begin().
  void begin() {
    label_ = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_14, 0);
    // Purely informational -- never intercepts taps meant for whatever
    // screen/overlay is underneath.
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_CLICKABLE);
    render();
    // NOT a corner -- this display is round, and every corner-placed
    // element in this codebase has had to be moved to top-center after
    // turning out clipped by the physical bezel on real hardware (see
    // ScreenManager.cpp's back/scan button comments, ADR 0004). Centered
    // on kRowCenterY using the label's actual rendered height (computed
    // after render() sets its text above) rather than a guessed offset,
    // so it lines up with the back/scan button regardless of font
    // metrics.
    lv_obj_update_layout(label_);
    int32_t halfHeight = lv_obj_get_height(label_) / 2;
    lv_obj_align(label_, LV_ALIGN_TOP_MID, kOffsetX, kRowCenterY - halfHeight);
  }

  // Call periodically (e.g. every few seconds from loop()) with a fresh
  // ADC millivolt reading.
  void update(uint32_t milliVolts) {
    monitor_.update(milliVolts);
    render();
  }

 private:
  void render() {
    using Level = power::BatteryMonitor::Level;
    const char *symbol;
    lv_color_t color;
    switch (monitor_.level()) {
      case Level::kEmpty:
        symbol = LV_SYMBOL_BATTERY_EMPTY;
        color = lv_palette_main(LV_PALETTE_RED);
        break;
      case Level::kLow:
        symbol = LV_SYMBOL_BATTERY_1;
        color = lv_palette_main(LV_PALETTE_RED);
        break;
      case Level::kMedium:
        symbol = LV_SYMBOL_BATTERY_2;
        color = lv_palette_main(LV_PALETTE_YELLOW);
        break;
      case Level::kHigh:
        symbol = LV_SYMBOL_BATTERY_3;
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
      case Level::kFull:
      default:
        symbol = LV_SYMBOL_BATTERY_FULL;
        color = lv_palette_main(LV_PALETTE_GREEN);
        break;
    }
    lv_label_set_text(label_, symbol);
    lv_obj_set_style_text_color(label_, color, 0);
  }

  power::BatteryMonitor &monitor_;
  lv_obj_t *label_ = nullptr;
};

}  // namespace knobify::ui
