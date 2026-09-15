// The main menu and settings screens (ADR 0010) -- ScreenManager methods
// kept apart from the music screens in ScreenManager.cpp. Settings itself
// is a plain list and renders through ScreenManager::renderList().
#include <Arduino.h>

#include <algorithm>
#include <cstdio>

#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "ScreenHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "Theme.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;

namespace knobify::ui {

namespace {

// One row per main-menu entry; adding a destination means adding a row
// here (and, beyond three entries, revisiting the tile row below on the
// device).
struct MenuEntry {
  const char *icon;
  const char *label;
  void (*open)(navigation::TabController &tabs);
  // The label shows the sleep timer's time left while it runs (ADR 0015).
  bool showsSleepTimer = false;
};

constexpr MenuEntry kMenuEntries[] = {
    {KNOBIFY_ICON_MUSIC_NOTE, "Music",
     [](navigation::TabController &tabs) { tabs.openMusic(); }},
    {KNOBIFY_ICON_SETTINGS, "Settings",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
     }},
    {KNOBIFY_ICON_BEDTIME, "Sleep",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::SleepTimer, {}});
     },
     true},
};
constexpr int kMenuEntryCount =
    static_cast<int>(sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));

// A solid mark the calibration screen draws at (cx, cy): a cross bar or a
// dot, centered there.
lv_obj_t *makeMark(lv_obj_t *parent, lv_coord_t cx, lv_coord_t cy,
                   lv_coord_t w, lv_coord_t h, lv_color_t color) {
  lv_obj_t *mark = lv_obj_create(parent);
  lv_obj_set_size(mark, w, h);
  lv_obj_set_pos(mark, cx - w / 2, cy - h / 2);
  lv_obj_set_style_bg_color(mark, color, 0);
  lv_obj_set_style_border_width(mark, 0, 0);
  lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(mark, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
  return mark;
}

// Tile geometry (ADR 0015): one row of three, circles 22px apart -- more
// than the 20px two touch-slop margins need (ux-guidelines §3a). They span
// x=32..328 at y=96..180, where the bezel shows ~21..339 at the top edge
// and ~5..355 at the middle; labels end above the mini-bar zone (y>=272).
// The row doesn't move with the mini-bar, so the menu never jumps when
// playback starts. A 2x2 grid of 112px tiles didn't fit three: the second
// row ran into the mini-bar and its label behind the bezel.
constexpr lv_coord_t kTileSize = 84;
constexpr lv_coord_t kTileCenterDx = 106;
constexpr lv_coord_t kTileTopY = 96;
constexpr lv_coord_t kCellHeight = kTileSize + 34;

// "Off" or "25 min".
void formatSleepMinutes(char *out, size_t size, uint32_t minutes) {
  if (minutes == 0) {
    snprintf(out, size, "Off");
  } else {
    snprintf(out, size, "%u min", static_cast<unsigned>(minutes));
  }
}

}  // namespace

void ScreenManager::renderHome() {
  // No caption, back button or title: the tiles say everything there is
  // to say here (ux-guidelines §5).
  tiles_ = lv_obj_create(screen_);
  lv_obj_set_size(tiles_, drivers::kLcdHorRes, drivers::kLcdVerRes);
  lv_obj_align(tiles_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_opa(tiles_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tiles_, 0, 0);
  lv_obj_set_style_pad_all(tiles_, 0, 0);
  lv_obj_clear_flag(tiles_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(tiles_, LV_OBJ_FLAG_CLICKABLE);

  for (int i = 0; i < kMenuEntryCount; ++i) {
    // The cell (circle + label) is the tap target, so the label is
    // tappable too.
    lv_obj_t *cell = lv_obj_create(tiles_);
    lv_obj_set_size(cell, kTileSize, kCellHeight);
    lv_coord_t dx = static_cast<lv_coord_t>((2 * i - (kMenuEntryCount - 1)) *
                                            kTileCenterDx / 2);
    lv_obj_align(cell, LV_ALIGN_TOP_MID, dx, kTileTopY);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(cell, 10);

    // Unselected like a Secondary button, selected like a selected list
    // row (ink) -- selection, not action, so never accent (ux-guidelines
    // §3a). The glyph inherits the circle's text color in either state.
    lv_obj_t *circle = lv_obj_create(cell);
    lv_obj_set_size(circle, kTileSize, kTileSize);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_shadow_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(circle, theme::surfaceAlt(), 0);
    lv_obj_set_style_text_color(circle, theme::ink(), 0);
    lv_obj_set_style_bg_color(circle, theme::ink(), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(circle, theme::surface(), LV_STATE_CHECKED);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

    // Font before text -- see makeIconButton()'s comment.
    lv_obj_t *glyph = lv_label_create(circle);
    lv_obj_set_style_text_font(glyph, &knobify_icon_font_48, 0);
    lv_label_set_text(glyph, kMenuEntries[i].icon);
    lv_obj_center(glyph);

    lv_obj_t *label = lv_label_create(cell);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, theme::ink(), 0);
    lv_label_set_text(label, kMenuEntries[i].label);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kTileSize + 8);
    if (kMenuEntries[i].showsSleepTimer) sleepTileLabel_ = label;

    // Touch selects on press and opens on release, so the finger and the
    // knob drive the same visible selection.
    lv_obj_add_event_cb(cell, &ScreenManager::onHomeTilePressed,
                        LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(cell, &ScreenManager::onHomeTileClicked,
                        LV_EVENT_CLICKED, this);
  }

  highlightedIndex_ = std::min(homeSelection_, kMenuEntryCount - 1);
  applyHighlight();

  tickSleepTimer(millis());

  if (playback_.state() != playback::PlaybackState::Stopped) renderMiniBar();
}

void ScreenManager::onHomeTilePressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  lv_obj_t *cell = lv_event_get_current_target(e);
  self->highlightedIndex_ = static_cast<int>(lv_obj_get_index(cell));
  self->homeSelection_ = self->highlightedIndex_;
  self->applyHighlight();
}

void ScreenManager::onHomeTileClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  int index = static_cast<int>(lv_obj_get_index(lv_event_get_current_target(e)));
  if (index < 0 || index >= kMenuEntryCount) return;
  self->homeSelection_ = index;
  kMenuEntries[index].open(self->tabs_);
  self->render();
}

void ScreenManager::renderBrightness() {
  // One value, set with the knob: a ring at the bezel (like the volume
  // ring, and in accent for the same reason -- a value being set) plus
  // the number. Changes apply to the backlight immediately, so the screen
  // itself is the preview. No mini-bar: nothing else competes here.
  constexpr lv_coord_t kGlyphY = 104;

  lv_obj_t *glyph = lv_label_create(screen_);
  lv_obj_set_style_text_font(glyph, &knobify_icon_font_48, 0);
  lv_obj_set_style_text_color(glyph, theme::structure(), 0);
  lv_label_set_text(glyph, KNOBIFY_ICON_LIGHT_MODE);
  lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, kGlyphY);

  brightnessLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(brightnessLabel_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(brightnessLabel_, theme::ink(), 0);
  lv_obj_align(brightnessLabel_, LV_ALIGN_TOP_MID, 0, kGlyphY + 60);

  // There's no button to press on this screen, so name the one control
  // that does something.
  lv_obj_t *hint = lv_label_create(screen_);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, theme::structure(), 0);
  lv_label_set_text(hint, "Turn to adjust");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, kGlyphY + 104);

  ui_widgets::EdgeArcConfig arcConfig;
  arcConfig.startAngle = 135;
  arcConfig.endAngle = 45;
  arcConfig.widthPx = 12;
  arcConfig.color = theme::accent();
  arcConfig.hasBackgroundColor = true;
  arcConfig.backgroundColor = theme::surfaceAlt();
  brightnessArcHost_ = makeEdgeArcHost(screen_);
  // From 0, so the lowest level still shows a sliver of ring: it is 10%,
  // not off.
  brightnessArc_.create(brightnessArcHost_, arcConfig, 0,
                        power::BrightnessSetting::kMaxLevel);

  updateBrightnessDisplay();
}

void ScreenManager::updateBrightnessDisplay() {
  if (!brightnessArcHost_ || !brightnessLabel_) return;
  brightnessArc_.setValue(static_cast<int32_t>(brightness_.level()));
  char text[8];
  snprintf(text, sizeof(text), "%u%%",
           static_cast<unsigned>(brightness_.percent()));
  lv_label_set_text(brightnessLabel_, text);
}

void ScreenManager::renderSleepTimer() {
  // Laid out like Brightness: one value, set with the knob, an accent ring
  // at the bezel. The ring counts down while the timer runs, on a scale of
  // the longest preset.
  constexpr lv_coord_t kGlyphY = 104;

  lv_obj_t *glyph = lv_label_create(screen_);
  lv_obj_set_style_text_font(glyph, &knobify_icon_font_48, 0);
  lv_obj_set_style_text_color(glyph, theme::structure(), 0);
  lv_label_set_text(glyph, KNOBIFY_ICON_BEDTIME);
  lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, kGlyphY);

  sleepValueLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(sleepValueLabel_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(sleepValueLabel_, theme::ink(), 0);
  lv_obj_align(sleepValueLabel_, LV_ALIGN_TOP_MID, 0, kGlyphY + 60);

  lv_obj_t *hint = lv_label_create(screen_);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(hint, theme::structure(), 0);
  lv_label_set_text(hint, "Turn to set");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, kGlyphY + 104);

  ui_widgets::EdgeArcConfig arcConfig;
  arcConfig.startAngle = 135;
  arcConfig.endAngle = 45;
  arcConfig.widthPx = 12;
  arcConfig.color = theme::accent();
  arcConfig.hasBackgroundColor = true;
  arcConfig.backgroundColor = theme::surfaceAlt();
  sleepArcHost_ = makeEdgeArcHost(screen_);
  sleepArc_.create(sleepArcHost_, arcConfig, 0,
                   power::SleepTimer::kPresetsMin[power::SleepTimer::kPresetCount - 1] *
                       60);

  tickSleepTimer(millis());
}

void ScreenManager::tickSleepTimer(uint32_t nowMs) {
  if (!sleepValueLabel_ && !sleepTileLabel_) return;
  uint32_t minutes = sleepTimer_.remainingMinutesCeil(nowMs);
  uint32_t seconds = (sleepTimer_.remainingMs(nowMs) + 999) / 1000;
  if (sleepArcHost_ && seconds != shownSleepSeconds_) {
    sleepArc_.setValue(static_cast<int32_t>(seconds));
  }
  shownSleepSeconds_ = seconds;
  if (minutes == shownSleepMinutes_) return;
  shownSleepMinutes_ = minutes;
  char text[16];
  if (sleepValueLabel_) {
    formatSleepMinutes(text, sizeof(text), minutes);
    lv_label_set_text(sleepValueLabel_, text);
  }
  if (sleepTileLabel_) {
    if (minutes == 0) {
      lv_label_set_text(sleepTileLabel_, "Sleep");
    } else {
      formatSleepMinutes(text, sizeof(text), minutes);
      lv_label_set_text(sleepTileLabel_, text);
    }
  }
}

void ScreenManager::renderTouchCalibration() {
  using input::CalibrationPhase;
  using input::TouchCalibrator;
  shownCalibrationPhase_ = touchCalibration_.phase();
  shownCalibrationTargets_ = touchCalibration_.targetsDone();
  shownCalibrationRejected_ = touchCalibration_.lastFitRejected();
  auto addLabel = [this](const char *text, const lv_font_t *font,
                         lv_color_t color, lv_coord_t dy) {
    lv_obj_t *label = lv_label_create(screen_);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
    return label;
  };

  if (shownCalibrationPhase_ == CalibrationPhase::Verifying) {
    // The new calibration is already live: tapping Keep proves it works.
    // If it doesn't, Keep can't be hit and the ring runs out.
    addLabel("Touch calibrated", &lv_font_montserrat_20, theme::ink(), -84);
    makeIconButton(screen_, "Keep", 96, 96, LV_ALIGN_CENTER, 0, 0,
                   &ScreenManager::onCalibrationKeepClicked, this,
                   ButtonRole::Primary, &lv_font_montserrat_20);
    addLabel("Reverts unless kept", &lv_font_montserrat_14, theme::structure(),
             84);
    ui_widgets::EdgeArcConfig arcConfig;
    arcConfig.startAngle = 135;
    arcConfig.endAngle = 45;
    arcConfig.widthPx = 12;
    arcConfig.color = theme::accent();
    arcConfig.hasBackgroundColor = true;
    arcConfig.backgroundColor = theme::surfaceAlt();
    calibrationArcHost_ = makeEdgeArcHost(screen_);
    calibrationArc_.create(calibrationArcHost_, arcConfig, 0,
                           input::TouchCalibrationFlow::kVerifyTimeoutMs);
    calibrationArc_.setValue(static_cast<int32_t>(
        touchCalibration_.verifyRemainingMs(millis())));
    return;
  }

  // Capturing: one accent cross at a time, taken targets as quiet dots.
  for (size_t i = 0; i < TouchCalibrator::kTargetCount; ++i) {
    const auto &t = TouchCalibrator::kTargets[i];
    if (i < shownCalibrationTargets_) {
      makeMark(screen_, t.x, t.y, 10, 10, theme::structure());
    } else if (i == shownCalibrationTargets_) {
      makeMark(screen_, t.x, t.y, 36, 4, theme::accent());
      makeMark(screen_, t.x, t.y, 4, 36, theme::accent());
    }
  }
  addLabel(shownCalibrationRejected_ ? "Didn't fit.\nTry again" : "Tap the cross",
           &lv_font_montserrat_20, theme::ink(),
           shownCalibrationRejected_ ? -24 : -16);
  char progress[16];
  snprintf(progress, sizeof(progress), "%u of %u",
           static_cast<unsigned>(shownCalibrationTargets_ + 1),
           static_cast<unsigned>(TouchCalibrator::kTargetCount));
  addLabel(progress, &lv_font_montserrat_14, theme::structure(), 18);
  // The one exit, and it never depends on touch.
  addLabel("Turn knob to cancel", &lv_font_montserrat_14, theme::structure(),
           52);
}

void ScreenManager::onCalibrationKeepClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  // Leaving the screen happens in tickTouchCalibration(), outside this
  // event.
  self->touchCalibration_.keep();
}

void ScreenManager::tickTouchCalibration(uint32_t nowMs) {
  using input::CalibrationOutcome;
  touchCalibration_.tick(nowMs);
  bool onScreen = tabs_.activeStack().current().kind == ScreenKind::TouchCalibration;
  // Left without finishing (a swipe back during Keep): don't keep it.
  if (!onScreen) touchCalibration_.cancel();

  CalibrationOutcome outcome = touchCalibration_.takeOutcome();
  if (outcome != CalibrationOutcome::None) {
    const input::TouchCalibration &cal = touchCalibration_.active();
    Serial.printf("[touchcal] %s: x scale=%d offset=%d, y scale=%d offset=%d\n",
                  outcome == CalibrationOutcome::Saved      ? "saved"
                  : outcome == CalibrationOutcome::Reverted ? "reverted"
                                                            : "cancelled",
                  cal.xScaleMilli, cal.xOffset, cal.yScaleMilli, cal.yOffset);
    if (onScreen) tabs_.back();
    render();
    constexpr ui_widgets::MessageAnchor kAnchor{drivers::kLcdHorRes / 2,
                                                drivers::kLcdVerRes / 2};
    if (outcome == CalibrationOutcome::Saved) {
      messages_.show("Touch calibration saved", kAnchor, nowMs);
    } else if (outcome == CalibrationOutcome::Reverted) {
      messages_.show("Not saved", kAnchor, nowMs);
    }
    return;
  }

  if (!onScreen || renderedKind_ != ScreenKind::TouchCalibration) return;
  if (touchCalibration_.phase() != shownCalibrationPhase_ ||
      touchCalibration_.targetsDone() != shownCalibrationTargets_ ||
      touchCalibration_.lastFitRejected() != shownCalibrationRejected_) {
    render();
  } else if (calibrationArcHost_) {
    calibrationArc_.setValue(
        static_cast<int32_t>(touchCalibration_.verifyRemainingMs(nowMs)));
  }
}

}  // namespace knobify::ui
