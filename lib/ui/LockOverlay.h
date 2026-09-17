#pragma once

#include <lvgl.h>

#include "EdgeArc.h"
#include "IconFont.h"
#include "LockController.h"
#include "LvglButtonHelpers.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"

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
    // Light like every other screen (ux-guidelines §3 "Why a light
    // theme") -- an earlier black overlay was the one dark screen left.
    lv_obj_set_style_bg_color(root_, theme::surface(), 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_radius(root_, 0, 0);
    // Clickable (even with no click handler of its own) so it absorbs
    // taps on the locked background rather than letting them fall
    // through to the screen underneath.
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(root_);
    lv_obj_set_style_text_font(label, &knobify_text_font_20, 0);
    lv_label_set_text(label, "Locked");
    // Set explicitly rather than inherited: this root lives on
    // lv_layer_top(), not a themed screen (same class of bug as ADR
    // 0004's dark-on-dark list rows).
    lv_obj_set_style_text_color(label, theme::ink(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -84);

    ui_widgets::EdgeArcConfig arcConfig;
    arcConfig.startAngle = 135;
    arcConfig.endAngle = 45;
    arcConfig.widthPx = 12;
    // Confirmation green, deliberately distinct from the Now Playing
    // volume (orange) and song-progress (yellow) rings -- similar-looking
    // indicators carry their meaning in their color (ux-guidelines §3).
    arcConfig.color = theme::confirm();
    arcConfig.hasBackgroundColor = true;
    arcConfig.backgroundColor = theme::surfaceAlt();
    // Oversized beyond the display's own bounds, same fix as the Now
    // Playing volume ring (ScreenManager::renderNowPlaying()) -- an
    // lv_arc's default radius stops short of its host's edge (knob
    // padding, see EdgeArc::create()), and even a host sized to exactly
    // match the framebuffer still left a gap from the true round bezel.
    lv_obj_t *arcHost = lv_obj_create(root_);
    lv_obj_set_size(arcHost, drivers::kLcdHorRes + 40,
                     drivers::kLcdVerRes + 40);
    lv_obj_align(arcHost, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(arcHost, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arcHost, 0, 0);
    lv_obj_clear_flag(arcHost, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(arcHost, LV_OBJ_FLAG_CLICKABLE);
    progressArc_.create(arcHost, arcConfig, 0, 100);
    progressArc_.setValue(0.0f);

    unlockButton_ =
        makeHoldButton(root_, KNOBIFY_ICON_LOCK_OPEN, 96, 96, LV_ALIGN_CENTER,
                       0, 0, &LockOverlay::onUnlockPressed,
                       &LockOverlay::onUnlockReleased, this,
                       ButtonRole::Primary, &knobify_icon_font_28);

    // Wordless hint (the pulsing ring, started/stopped below) plus a
    // plain-text one -- neither the button nor the ring alone made the
    // hold-AND-turn gesture guessable (user feedback 2026-09-13: "ist mir
    // nicht ganz klar").
    hintLabel_ = lv_label_create(root_);
    lv_label_set_text(hintLabel_, "Hold & turn to unlock");
    lv_obj_set_style_text_color(hintLabel_, theme::structure(), 0);
    lv_obj_align(hintLabel_, LV_ALIGN_CENTER, 0, 84);

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
        startPulse();
      } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        stopPulse();
      }
    }
    if (visible_) {
      progressArc_.setValue(lockController_.unlockProgress());
    }
  }

  bool isVisible() const { return visible_; }

 private:
  // Gently breathes the ring's unfilled (grey) track's opacity, inviting
  // interaction wordlessly -- paused while the user is actually holding
  // the button (real green progress is the feedback then instead) and
  // resumed if they let go before unlocking.
  void startPulse() {
    if (pulsing_) return;
    pulsing_ = true;
    lv_anim_init(&pulseAnim_);
    lv_anim_set_var(&pulseAnim_, this);
    lv_anim_set_exec_cb(&pulseAnim_, &LockOverlay::pulseAnimCb);
    lv_anim_set_values(&pulseAnim_, 90, 255);
    lv_anim_set_time(&pulseAnim_, 900);
    lv_anim_set_playback_time(&pulseAnim_, 900);
    lv_anim_set_repeat_count(&pulseAnim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&pulseAnim_);
  }

  void stopPulse() {
    if (!pulsing_) return;
    pulsing_ = false;
    lv_anim_del(this, &LockOverlay::pulseAnimCb);
    progressArc_.setBackgroundOpacity(LV_OPA_COVER);
  }

  static void pulseAnimCb(void *var, int32_t value) {
    static_cast<LockOverlay *>(var)->progressArc_.setBackgroundOpacity(
        static_cast<lv_opa_t>(value));
  }

  static void onUnlockPressed(lv_event_t *e) {
    auto *self = static_cast<LockOverlay *>(lv_event_get_user_data(e));
    self->stopPulse();
    self->lockController_.onHoldStart(lv_tick_get());
  }

  static void onUnlockReleased(lv_event_t *e) {
    auto *self = static_cast<LockOverlay *>(lv_event_get_user_data(e));
    self->lockController_.onHoldEnd();
    // Still locked (didn't reach the threshold) -- resume inviting
    // another try. If it just unlocked, tick() will hide the overlay
    // (and stop the pulse) on its next call instead.
    if (self->lockController_.isLocked()) self->startPulse();
  }

  power::LockController &lockController_;
  lv_obj_t *root_ = nullptr;
  lv_obj_t *unlockButton_ = nullptr;
  lv_obj_t *hintLabel_ = nullptr;
  ui_widgets::EdgeArc progressArc_;
  lv_anim_t pulseAnim_{};
  bool pulsing_ = false;
  bool visible_ = false;
};

}  // namespace knobify::ui
