// The main menu and settings screens (ADR 0010) -- ScreenManager methods
// kept apart from the music screens in ScreenManager.cpp. Settings itself
// is a plain list and renders through ScreenManager::renderList().
#include <algorithm>
#include <cstdio>

#include "IconFont.h"
#include "ScreenHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "Theme.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;

namespace knobify::ui {

namespace {

// One row per main-menu entry; adding a destination means adding a row
// here (and, beyond four entries, revisiting the grid below on the
// device).
struct MenuEntry {
  const char *icon;
  const char *label;
  void (*open)(navigation::TabController &tabs);
};

constexpr MenuEntry kMenuEntries[] = {
    {KNOBIFY_ICON_MUSIC_NOTE, "Music",
     [](navigation::TabController &tabs) { tabs.openMusic(); }},
    {KNOBIFY_ICON_SETTINGS, "Settings",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
     }},
};
constexpr int kMenuEntryCount =
    static_cast<int>(sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));

// Tile geometry. Two columns whose circles sit 28px apart -- more than the
// 20px two touch-slop margins need (ux-guidelines §3a). The first row's
// circles span x=54..306 at y=88..200, inside the ~25..335 the bezel
// shows at y=88; the row's position doesn't move with the mini-bar, so
// the menu never jumps when playback starts.
constexpr lv_coord_t kTileSize = 112;
constexpr lv_coord_t kTileCenterDx = 70;
constexpr lv_coord_t kTileTopY = 88;
constexpr lv_coord_t kTileRowPitch = 150;
constexpr lv_coord_t kCellHeight = kTileSize + 34;

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
    lv_coord_t dx = (i % 2 == 0) ? -kTileCenterDx : kTileCenterDx;
    if (i == kMenuEntryCount - 1 && i % 2 == 0) dx = 0;  // Odd one out centers.
    lv_obj_align(cell, LV_ALIGN_TOP_MID, dx, kTileTopY + (i / 2) * kTileRowPitch);
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

    // Touch selects on press and opens on release, so the finger and the
    // knob drive the same visible selection.
    lv_obj_add_event_cb(cell, &ScreenManager::onHomeTilePressed,
                        LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(cell, &ScreenManager::onHomeTileClicked,
                        LV_EVENT_CLICKED, this);
  }

  highlightedIndex_ = std::min(homeSelection_, kMenuEntryCount - 1);
  applyHighlight();

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

}  // namespace knobify::ui
