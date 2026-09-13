#pragma once

#include <lvgl.h>

#include "EdgeArc.h"
#include "IconFont.h"
#include "LockController.h"
#include "LvglButtonHelpers.h"

namespace knobify::ui {

// The locked-device UI: a full-screen overlay with an unlock button and
// a progress ring, shown/hidden based on LockController::isLocked()
// rather than pushed onto NavigationStack -- see ADR 0005 for why (keeps
// lock concerns entirely out of ScreenManager/NavigationStack/
// TabController, avoids special-casing the stack depth cap and
// swipe-back).
//
// Lives on LVGL's top layer (lv_layer_top()), which LVGL always draws
// and hit-tests above the active screen -- so simply showing/hiding this
// overlay is enough to block taps from reaching whatever ScreenManager
// last rendered underneath, no coordination with ScreenManager needed.
//
// Thin and LVGL-facing like ScreenManager -- not host-tested. The actual
// unlock logic (hold+turn state machine) lives in the host-tested
// power::LockController; this class only turns LVGL press/release events
// on its button into onHoldStart()/onHoldEnd() calls and renders
// unlockProgress() as a ring.
class LockOverlay {
 public:
  explicit LockOverlay(power::LockController &lockController)
      : lockController_(lockController) {}

  // Call once, after LVGL is initialized (e.g. from ScreenManager::begin()
  // or main.cpp's setup()).
  void begin() {
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    // Clickable (even with no click handler of its own) so it absorbs
    // taps on the locked background rather than letting them fall
    // through to the screen underneath.
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(root_);
    lv_label_set_text(label, "Locked");
    // The app's light theme's default label color is dark text meant for
    // a light background -- invisible against this overlay's black
    // background (same class of bug as ADR 0004's dark-on-dark list
    // rows). Must be set explicitly, not inherited.
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -70);

    ui_widgets::EdgeArcConfig arcConfig;
    arcConfig.startAngle = 135;
    arcConfig.endAngle = 45;
    arcConfig.widthPx = 12;
    // Green, deliberately distinct from the Now Playing volume ring's
    // indigo -- see ScreenManager::renderNowPlaying()'s comment.
    arcConfig.color = lv_palette_main(LV_PALETTE_GREEN);
    arcConfig.hasBackgroundColor = true;
    arcConfig.backgroundColor = lv_palette_lighten(LV_PALETTE_GREY, 1);
    lv_obj_t *arcHost = lv_obj_create(root_);
    lv_obj_set_size(arcHost, 260, 260);
    lv_obj_align(arcHost, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(arcHost, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arcHost, 0, 0);
    lv_obj_clear_flag(arcHost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(arcHost, LV_OBJ_FLAG_CLICKABLE);
    progressArc_.create(arcHost, arcConfig, 0, 100);
    progressArc_.setValue(0.0f);

    unlockButton_ =
        makeHoldButton(root_, KNOBIFY_ICON_LOCK_OPEN, 90, 90, LV_ALIGN_CENTER,
                       0, 40, &LockOverlay::onUnlockPressed,
                       &LockOverlay::onUnlockReleased, this,
                       &knobify_icon_font_28);

    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
  }

  // Call once per loop(): shows/hides the overlay to match lock state,
  // and refreshes the progress ring while held.
  void tick() {
    bool locked = lockController_.isLocked();
    if (locked != visible_) {
      visible_ = locked;
      if (visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (visible_) {
      progressArc_.setValue(lockController_.unlockProgress());
    }
  }

  bool isVisible() const { return visible_; }

 private:
  static void onUnlockPressed(lv_event_t *e) {
    auto *self = static_cast<LockOverlay *>(lv_event_get_user_data(e));
    self->lockController_.onHoldStart(lv_tick_get());
  }

  static void onUnlockReleased(lv_event_t *e) {
    auto *self = static_cast<LockOverlay *>(lv_event_get_user_data(e));
    self->lockController_.onHoldEnd();
  }

  power::LockController &lockController_;
  lv_obj_t *root_ = nullptr;
  lv_obj_t *unlockButton_ = nullptr;
  ui_widgets::EdgeArc progressArc_;
  bool visible_ = false;
};

}  // namespace knobify::ui
