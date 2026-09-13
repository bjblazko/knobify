#pragma once

#include <lvgl.h>

#include <cstdio>

#include "BatteryMonitor.h"
#include "Theme.h"

namespace knobify::ui {

// Battery status, shown only when it matters (docs/design/ux-guidelines.md
// §6): hidden during normal use, a red icon + percentage when the charge
// is low, and always shown (neutral) on the lock screen -- the natural
// moment to glance at status, one tap away via the lock button. An
// always-on icon offset beside the back/scan button (the earlier design)
// was the only off-axis element on screen and read like a tappable
// toolbar icon.
//
// Lives on LVGL's top layer (lv_layer_top()) -- same trick as LockOverlay
// (see that class's comment for why): drawn above whatever ScreenManager
// last rendered, with zero coordination needed from ScreenManager itself.
// Construct/begin() this AFTER LockOverlay so it z-orders above it too
// (later-created top-layer children draw on top).
//
// Thin and LVGL-facing, not host-tested -- the actual percent/level
// logic lives in the host-tested power::BatteryMonitor; this class only
// renders it.
class BatteryIndicator {
 public:
  explicit BatteryIndicator(power::BatteryMonitor &monitor)
      : monitor_(monitor) {}

  // At or below this, the indicator appears during normal use.
  static constexpr int kShowAtOrBelowPercent = 20;
  // Centered on the vertical axis, never a corner (round bezel, see
  // ux-guidelines §4). Unlocked: the caption slot under the top control
  // (ScreenManager.h's kCaptionY), with an opaque background so it
  // replaces the caption while shown. Locked: above "Locked".
  static constexpr lv_coord_t kUnlockedY = 52;
  static constexpr lv_coord_t kLockedY = 48;

  // Call once, after LVGL is initialized and after LockOverlay::begin().
  void begin() {
    label_ = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(label_, theme::surface(), 0);
    lv_obj_set_style_bg_opa(label_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(label_, 8, 0);
    // Purely informational -- never intercepts taps meant for whatever
    // screen/overlay is underneath.
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_CLICKABLE);
    render();
  }

  // Call periodically (e.g. every few seconds from loop()) with a fresh
  // ADC millivolt reading.
  void update(uint32_t milliVolts) {
    monitor_.update(milliVolts);
    render();
  }

  // Call once per loop(), after LockOverlay::tick().
  void setLocked(bool locked) {
    if (locked == locked_) return;
    locked_ = locked;
    render();
  }

 private:
  void render() {
    using Level = power::BatteryMonitor::Level;
    const char *symbol;
    switch (monitor_.level()) {
      case Level::kEmpty:
        symbol = LV_SYMBOL_BATTERY_EMPTY;
        break;
      case Level::kLow:
        symbol = LV_SYMBOL_BATTERY_1;
        break;
      case Level::kMedium:
        symbol = LV_SYMBOL_BATTERY_2;
        break;
      case Level::kHigh:
        symbol = LV_SYMBOL_BATTERY_3;
        break;
      case Level::kFull:
      default:
        symbol = LV_SYMBOL_BATTERY_FULL;
        break;
    }
    int percent = monitor_.percent();
    bool low = percent <= kShowAtOrBelowPercent;
    if (!locked_ && !low) {
      lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    char text[24];
    snprintf(text, sizeof(text), "%s  %d%%", symbol, percent);
    lv_label_set_text(label_, text);
    // Red only when low -- otherwise neutral, even on the lock screen.
    lv_obj_set_style_text_color(label_,
                                low ? theme::warning() : theme::structure(), 0);
    lv_obj_align(label_, LV_ALIGN_TOP_MID, 0, locked_ ? kLockedY : kUnlockedY);
    lv_obj_clear_flag(label_, LV_OBJ_FLAG_HIDDEN);
  }

  power::BatteryMonitor &monitor_;
  lv_obj_t *label_ = nullptr;
  bool locked_ = false;
};

}  // namespace knobify::ui
