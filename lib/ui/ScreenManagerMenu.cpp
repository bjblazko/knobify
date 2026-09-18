// The main menu and settings screens (ADR 0010) -- ScreenManager methods
// kept apart from the music screens in ScreenManager.cpp. Settings itself
// is a plain list and renders through ScreenManager::renderList().
#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "ScreenHelpers.h"
#include "ScreenManager.h"
#include "St77916Driver.h"
#include "TextFont.h"
#include "Theme.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;

namespace knobify::ui {

// Music's browse axes (ADR 0021), in the order the picker lists them:
// the shelf you know first, the flatter ones after, the derived ones
// last. A table rather than a switch, so adding a shelf is one line.
const ScreenManager::BrowseAxis
    ScreenManager::kBrowseAxes[ScreenManager::kBrowseAxisCount] = {
        {"Artists", navigation::ScreenKind::Artists},
        {"Albums", navigation::ScreenKind::AlbumsFlat},
        {"Songs", navigation::ScreenKind::Songs},
        {"Years", navigation::ScreenKind::Years},
        {"Genres", navigation::ScreenKind::Genres},
};

// Only Music, and only on the shelf's own root: inside an artist, or a
// year, "browse by" would mean leaving where you are, which the back
// chevron already does.
bool ScreenManager::hasBrowseAxisRow(const navigation::Screen &screen) const {
  if (screen.params.collection != collection::CollectionId::Music) return false;
  if (tabs_.activeTab() != navigation::Tab::Library) return false;
  if (tabs_.activeStack().canGoBack()) return false;
  for (const auto &axis : kBrowseAxes) {
    if (axis.kind == screen.kind) return true;
  }
  return false;
}

void ScreenManager::openBrowseAxis(int axisIndex) {
  if (axisIndex < 0 || axisIndex >= kBrowseAxisCount) return;
  const auto kind = kBrowseAxes[axisIndex].kind;
  tabs_.setLibraryRoot(kind);
  settings_.setU8(kMusicAxisKey, static_cast<uint8_t>(kind));
  render();
}

// The shelf Music was left on. Called before any resume record is
// applied -- NavigationResumeSource only accepts a saved stack whose
// root matches the tab's current one.
void ScreenManager::restoreBrowseAxis() {
  uint8_t stored = 0;
  if (!settings_.getU8(kMusicAxisKey, stored)) return;
  const auto kind = static_cast<navigation::ScreenKind>(stored);
  for (const auto &axis : kBrowseAxes) {
    if (axis.kind != kind) continue;
    tabs_.setLibraryRoot(kind);
    return;
  }
}


namespace {

// One row per main-menu entry; adding a destination means adding a row
// here. Since ADR 0018 the row order is also the carousel's order and the
// bit order of the visibility setting, so rows are append-only: inserting
// one in the middle would silently re-point a user's hidden entries.
struct MenuEntry {
  const char *icon;
  const char *label;
  void (*open)(navigation::TabController &tabs);
  // The label shows the sleep timer's time left while it runs (ADR 0015).
  bool showsSleepTimer = false;
  // Settings is the only way back to this screen, so it is the one entry
  // the user may not hide.
  bool alwaysVisible = false;
};

constexpr MenuEntry kMenuEntries[] = {
    {KNOBIFY_ICON_MUSIC_NOTE, "Music",
     [](navigation::TabController &tabs) {
       tabs.openCollection(collection::CollectionId::Music);
     }},
    {KNOBIFY_ICON_MENU_BOOK, "Audiobooks",
     [](navigation::TabController &tabs) {
       tabs.openCollection(collection::CollectionId::Audiobooks);
     }},
    {KNOBIFY_ICON_THEATER_COMEDY, "Radio Plays",
     [](navigation::TabController &tabs) {
       tabs.openCollection(collection::CollectionId::RadioPlays);
     }},
    {KNOBIFY_ICON_SETTINGS, "Settings",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::Settings, {}});
     },
     /*showsSleepTimer=*/false, /*alwaysVisible=*/true},
    {KNOBIFY_ICON_BEDTIME, "Sleep",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::SleepTimer, {}});
     },
     /*showsSleepTimer=*/true},
    {KNOBIFY_ICON_GAMES, "Games",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::Games, {}});
     }},
    {KNOBIFY_ICON_TONES, "Tones",
     [](navigation::TabController &tabs) {
       tabs.activeStack().push(Screen{ScreenKind::ToneGenerator, {}});
     }},
};
constexpr int kMenuEntryCount =
    static_cast<int>(sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));
static_assert(kMenuEntryCount <= 8,
              "the visibility setting is one bit per entry in a uint8_t");

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

// Carousel geometry (ADR 0018). ADR 0015's row of three 84px tiles filled
// the display exactly; five destinations do not fit any row, so Home shows
// the selected tile at full size in the middle with its two neighbours
// shrunk and dimmed either side, and the knob rotates through them.
//
// Sizes are up from the first cut (84/56) on the device: with only one
// destination fully shown at a time there is room to make it read from
// further away, which is the point of a carousel. The outer edge still
// lands at x=32/328 (116 + 64/2 = 148 from centre), the span ADR 0015
// verified as inside the bezel, and the 36px between the centre tile and
// a neighbour is well clear of the 20px two touch-slop margins need
// (ux-guidelines §3a).
constexpr lv_coord_t kTileSize = 96;
constexpr lv_coord_t kSideTileSize = 64;
constexpr lv_coord_t kSideTileDx = 116;
// Pushed down from ADR 0015's y=96 to make room for the wordmark above.
// The whole stack now reads top to bottom: wordmark 44, tiles 112..208,
// label 218, dots 252 -- and still ends clear of the mini-bar zone
// (y>=272), so the menu never jumps when playback starts.
constexpr lv_coord_t kTileTopY = 112;
// The label is as wide as the screen allows rather than as wide as the
// tile: it lives on the carousel container, not inside the 96px cell,
// which clipped "Audiobooks" and "Radio Plays" at both ends (found on the
// device 2026-09-16). Only the centre tile is labelled -- three labels at
// this spacing overlapped.
constexpr lv_coord_t kLabelWidth = 280;
constexpr lv_coord_t kLabelDy = kTileTopY + kTileSize + 10;
// Page dots *below* the tiles, centred -- where a page indicator is
// conventionally read, and it leaves the top of the screen to the
// wordmark. Above the mini-bar zone (y>=272) so the menu never jumps when
// playback starts. Never a corner (ux-guidelines §7).
constexpr lv_coord_t kDotSize = 6;
constexpr lv_coord_t kDotSpacing = 14;
constexpr lv_coord_t kDotsY = 252;

// The wordmark, top-centre: a small dial and "knobify" in lowercase, set
// in an ink capsule. Home is the one screen with room for it -- no
// caption, title or back button, and it is the screen the device boots
// into.
//
// The mark is a knob seen from above: a disc with a pointer notched out
// of it, set a little past vertical so it reads as a dial at a setting
// rather than a full stop. Drawn light on ink, it stops reading as a
// bullet point in front of a word (which is what the same pair looked
// like on the bare surface, user, 2026-09-17) and starts reading as a
// mark -- a badge stamped on a front panel, the way a Braun device names
// itself. It carries no signal colour: every colour in this system means
// something (§3 rule 2) and a brand mark means nothing, so the whole
// badge is the two neutrals, ink and surface. Its form does the rest,
// echoing the round display and the rotary encoder the way the circular
// transport buttons do (Rams #1, #7).
constexpr lv_coord_t kWordmarkY = 40;
constexpr lv_coord_t kBadgeHeight = 26;
// Enough ink around the pair that the capsule reads as a deliberate
// shape rather than a tight box; the left inset also keeps the dial off
// the rounded end.
constexpr lv_coord_t kBadgePadX = 14;
constexpr lv_coord_t kDialSize = 15;
constexpr lv_coord_t kDialDotSize = 4;
// The indicator sits up and to the right of centre, inside the rim. Not
// straight up: a mark at twelve o'clock reads as "off" or as a full stop,
// one turned a little reads as a knob someone has set.
constexpr lv_coord_t kDialDotX = 8;
constexpr lv_coord_t kDialDotY = 3;
constexpr lv_coord_t kWordmarkGap = 8;
// Air between the letters, so the word reads as set rather than typed --
// the one typographic liberty taken anywhere in this UI, and only here,
// because this is the only string on screen that is a name rather than
// information. Widened from 1px to 3px (user, 2026-09-18): at 1px the
// letters still read as a default-spaced word, and a name badge on a
// front panel is tracked out on purpose. About 0.2em at this size, which
// is spaced enough to look deliberate and not so far that the word comes
// apart into letters.
//
// Everything else about the badge is measured from this: the capsule's
// width comes from lv_txt_get_size() with this value, so changing it
// needs no other number touched.
constexpr lv_coord_t kWordmarkTracking = 3;
constexpr const char *kWordmark = "knobify";

// Which carousel slot a tile sits in. Stored in the cell's user data so
// one click handler can tell "open this" from "rotate to this".
constexpr int kSlotLeft = -1;
constexpr int kSlotCentre = 0;
constexpr int kSlotRight = 1;

// "Off" or "25 min".
void formatSleepMinutes(char *out, size_t size, uint32_t minutes) {
  if (minutes == 0) {
    snprintf(out, size, "Off");
  } else {
    snprintf(out, size, "%u min", static_cast<unsigned>(minutes));
  }
}

}  // namespace

// Settings' rows. A table, so adding one never renumbers the handler for
// the rows after it -- which is exactly what the two ADR 0018 rows would
// otherwise have done to USB drive.
const ScreenManager::SettingsRow
    ScreenManager::kSettingsRows[ScreenManager::kSettingsRowCount] = {
        {"Brightness",
         [](ScreenManager &self) {
           self.tabs_.activeStack().push(Screen{ScreenKind::Brightness, {}});
           self.render();
         }},
        {"Touch calibration",
         [](ScreenManager &self) {
           self.touchCalibration_.start(millis());
           self.tabs_.activeStack().push(
               Screen{ScreenKind::TouchCalibration, {}});
           self.render();
         }},
        {"Rescan",
         [](ScreenManager &self) {
           self.tabs_.activeStack().push(Screen{ScreenKind::RescanPicker, {}});
           self.render();
         }},
        {"Main menu",
         [](ScreenManager &self) {
           self.tabs_.activeStack().push(
               Screen{ScreenKind::MenuVisibility, {}});
           self.render();
         }},
        {"USB drive", [](ScreenManager &self) { self.startUsbDrive(); }},
};


// The main-menu entries the user has not hidden, in table order. Settings
// is never hidden (MenuEntry::alwaysVisible), so this is never empty and
// the carousel always has somewhere to go.
std::vector<int> ScreenManager::visibleMenuEntries() const {
  std::vector<int> visible;
  for (int i = 0; i < kMenuEntryCount; ++i) {
    if (menuEntryVisible(i)) visible.push_back(i);
  }
  return visible;
}

navigation::MenuVisibility ScreenManager::makeMenuVisibility() {
  uint8_t pinned = 0;
  for (int i = 0; i < kMenuEntryCount; ++i) {
    if (kMenuEntries[i].alwaysVisible) pinned |= static_cast<uint8_t>(1u << i);
  }
  return navigation::MenuVisibility(kMenuEntryCount, pinned);
}

bool ScreenManager::menuEntryVisible(int entryIndex) const {
  return menuVisibility_.visible(entryIndex);
}

void ScreenManager::loadMenuVisibility() {
  uint8_t stored = navigation::MenuVisibility::kDefaultMask;
  if (settings_.getU8(kMenuVisibilityKey, stored)) {
    menuVisibility_.setMask(stored);
  }
}

void ScreenManager::toggleMenuEntryVisible(int entryIndex) {
  constexpr ui_widgets::MessageAnchor kAnchor{drivers::kLcdHorRes / 2,
                                              drivers::kLcdVerRes / 2};
  switch (menuVisibility_.toggle(entryIndex)) {
    case navigation::MenuVisibility::ToggleResult::Pinned:
      // Hiding Settings would hide this very screen. Say so rather than
      // letting the row look broken (Rams #4).
      messages_.show("Settings always shows", kAnchor, millis());
      return;
    case navigation::MenuVisibility::ToggleResult::WouldEmptyMenu:
      messages_.show("Keep at least one", kAnchor, millis());
      return;
    case navigation::MenuVisibility::ToggleResult::Toggled:
      break;
  }
  settings_.setU8(kMenuVisibilityKey, menuVisibility_.mask());
  // The carousel is rebuilt from the filtered list, so a hidden entry
  // must not leave the selection pointing past the end.
  homeSelection_ = 0;
  render();
}

void ScreenManager::appendMenuVisibilityRows(
    std::vector<std::pair<std::string, int>> &items) const {
  for (int i = 0; i < kMenuEntryCount; ++i) {
    items.emplace_back(kMenuEntries[i].label, i);
  }
}

// The trailing "On"/"Off" (or "Always") on a Main menu row -- plain text,
// like the Brightness row's percentage (ux-guidelines §7).
const char *ScreenManager::menuVisibilityValue(int entryIndex) const {
  if (entryIndex < 0 || entryIndex >= kMenuEntryCount) return "";
  if (menuVisibility_.pinned(entryIndex)) return "Always";
  return menuEntryVisible(entryIndex) ? "On" : "Off";
}

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

  const std::vector<int> visible = visibleMenuEntries();
  const int count = static_cast<int>(visible.size());
  sleepTileLabel_ = nullptr;
  renderWordmark();
  // Nothing visible should be impossible (MenuVisibility pins Settings and
  // refuses to hide the last entry), but this runs at boot on every
  // power-on and the slot maths below divides by `count` -- a stored byte
  // that somehow said "nothing" would hang the device before it ever
  // reached a screen where the user could fix it.
  if (count <= 0) return;
  if (homeSelection_ >= count || homeSelection_ < 0) homeSelection_ = 0;

  // Dots first, so the tiles draw over them if anything ever overlaps.
  // One per destination: the carousel shows one at a time, so this is the
  // only thing saying how many there are (Rams #4). A single destination
  // needs no dots -- there is nothing to page through.
  if (count > 1) {
    for (int i = 0; i < count; ++i) {
      lv_coord_t dx =
          static_cast<lv_coord_t>((2 * i - (count - 1)) * kDotSpacing / 2);
      lv_obj_t *dot = lv_obj_create(tiles_);
      lv_obj_set_size(dot, kDotSize, kDotSize);
      lv_obj_align(dot, LV_ALIGN_TOP_MID, dx, kDotsY);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(dot, 0, 0);
      lv_obj_set_style_pad_all(dot, 0, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      // The current one in ink, the rest in the same grey the unselected
      // tiles use -- selection, never accent (ux-guidelines §3a).
      lv_obj_set_style_bg_color(
          dot, i == homeSelection_ ? theme::ink() : theme::surfaceAlt(), 0);
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
  }

  // Centre tile last of the three, so it is on top of its neighbours if
  // they ever overlap.
  const int slots[] = {kSlotLeft, kSlotRight, kSlotCentre};
  for (int slot : slots) {
    const int offset = slot;
    if (offset != 0 && count < 2) continue;
    // Two destinations would otherwise show the same tile left and right.
    if (offset > 0 && count == 2) continue;
    const int entry =
        visible[static_cast<size_t>((homeSelection_ + offset + count) % count)];
    makeMenuTile(entry, slot);
  }

  tickSleepTimer(millis());

  if (playback_.state() != playback::PlaybackState::Stopped) renderMiniBar();
}

// The wordmark: the dial and "knobify" inverted inside an ink capsule,
// centred as one shape. The capsule is sized from the measured text and
// both parts are placed explicitly inside it, rather than put in a flex
// row -- every other screen here positions with lv_obj_align(), and the
// screen the device boots into is the last place to introduce a layout
// engine whose passes interact with the label clamping below.
void ScreenManager::renderWordmark() {
  const lv_font_t *font = &knobify_text_font_16;
  lv_point_t textSize;
  lv_txt_get_size(&textSize, kWordmark, font, kWordmarkTracking, 0,
                  LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  // LVGL adds the tracking after the last letter too; that trailing gap
  // is not ink, so it must not count toward the capsule's width or the
  // pair would sit a pixel left of centre inside it.
  const lv_coord_t wordWidth =
      static_cast<lv_coord_t>(textSize.x - kWordmarkTracking);
  const lv_coord_t content =
      static_cast<lv_coord_t>(kDialSize + kWordmarkGap + wordWidth);

  lv_obj_t *badge = lv_obj_create(tiles_);
  lv_obj_set_size(badge, static_cast<lv_coord_t>(content + 2 * kBadgePadX),
                  kBadgeHeight);
  lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, kWordmarkY);
  lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_shadow_width(badge, 0, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(badge, theme::ink(), 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *dial = lv_obj_create(badge);
  lv_obj_set_size(dial, kDialSize, kDialSize);
  lv_obj_align(dial, LV_ALIGN_LEFT_MID, kBadgePadX, 0);
  lv_obj_set_style_radius(dial, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dial, 0, 0);
  lv_obj_set_style_pad_all(dial, 0, 0);
  lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dial, theme::surface(), 0);
  lv_obj_clear_flag(dial, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dial, LV_OBJ_FLAG_CLICKABLE);

  // The indicator, notched out of the disc in the capsule's own ink, so
  // the mark stays two tones and reads at 15px.
  lv_obj_t *dot = lv_obj_create(dial);
  lv_obj_set_size(dot, kDialDotSize, kDialDotSize);
  lv_obj_align(dot, LV_ALIGN_TOP_LEFT, kDialDotX, kDialDotY);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, theme::ink(), 0);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *word = lv_label_create(badge);
  lv_obj_set_style_text_font(word, font, 0);
  lv_obj_set_style_text_letter_space(word, kWordmarkTracking, 0);
  lv_obj_set_style_text_color(word, theme::surface(), 0);
  lv_label_set_text(word, kWordmark);
  lv_obj_align(word, LV_ALIGN_LEFT_MID,
               static_cast<lv_coord_t>(kBadgePadX + kDialSize + kWordmarkGap),
               0);
}

// One carousel tile. The centre one is full size (96px,
// labelled, selected-looking); a neighbour is smaller, dimmed and
// unlabelled -- enough to say "there is more this way" without competing
// with the destination you are actually on (Rams #5).
void ScreenManager::makeMenuTile(int entryIndex, int slot) {
  const bool centre = slot == kSlotCentre;
  const lv_coord_t size = centre ? kTileSize : kSideTileSize;
  const lv_coord_t dx =
      static_cast<lv_coord_t>(slot * kSideTileDx);
  // Neighbours sit level with the centre tile's circle, not its cell.
  const lv_coord_t top =
      centre ? kTileTopY
             : static_cast<lv_coord_t>(kTileTopY + (kTileSize - size) / 2);

  // The cell (circle + label) is the tap target, so the label is
  // tappable too.
  lv_obj_t *cell = lv_obj_create(tiles_);
  lv_obj_set_size(cell, size, size);
  lv_obj_align(cell, LV_ALIGN_TOP_MID, dx, top);
  lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cell, 0, 0);
  lv_obj_set_style_pad_all(cell, 0, 0);
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(cell, 10);

  // Centre: ink with a surface glyph, like a selected list row --
  // selection, not action, so never accent (ux-guidelines §3a).
  // Neighbours: the unselected Secondary look, at half opacity.
  lv_obj_t *circle = lv_obj_create(cell);
  lv_obj_set_size(circle, size, size);
  lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(circle, 0, 0);
  lv_obj_set_style_shadow_width(circle, 0, 0);
  lv_obj_set_style_pad_all(circle, 0, 0);
  lv_obj_set_style_bg_opa(circle, centre ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_set_style_bg_color(circle, centre ? theme::ink() : theme::surfaceAlt(),
                            0);
  lv_obj_set_style_text_color(circle, centre ? theme::surface() : theme::ink(),
                              0);
  lv_obj_set_style_text_opa(circle, centre ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(circle, LV_OBJ_FLAG_CLICKABLE);

  // Font before text -- see makeIconButton()'s comment.
  lv_obj_t *glyph = lv_label_create(circle);
  lv_obj_set_style_text_font(
      glyph, centre ? &knobify_icon_font_48 : &knobify_icon_font_28, 0);
  lv_label_set_text(glyph, kMenuEntries[entryIndex].icon);
  lv_obj_center(glyph);

  if (centre) {
    // On tiles_, not the cell: a label inside a 96px cell is clipped to
    // it. It sits below the neighbour tiles, so a full-width label
    // overlaps nothing, and it opens the centre tile
    // like the circle does -- the label has always been part of the
    // target.
    lv_obj_t *label = lv_label_create(tiles_);
    lv_obj_set_style_text_font(label, &knobify_text_font_16, 0);
    lv_obj_set_style_text_color(label, theme::ink(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, kLabelWidth);
    setClampedText(label, kMenuEntries[entryIndex].label, 1);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kLabelDy);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(
        label, reinterpret_cast<void *>(static_cast<intptr_t>(kSlotCentre)));
    lv_obj_add_event_cb(label, &ScreenManager::onHomeTileClicked,
                        LV_EVENT_CLICKED, this);
    if (kMenuEntries[entryIndex].showsSleepTimer) sleepTileLabel_ = label;
  }

  // A tap on the centre opens it; a tap on a neighbour rotates that one in
  // rather than opening a destination the user cannot fully see.
  lv_obj_set_user_data(
      cell, reinterpret_cast<void *>(static_cast<intptr_t>(slot)));
  lv_obj_add_event_cb(cell, &ScreenManager::onHomeTileClicked, LV_EVENT_CLICKED,
                      this);
}

void ScreenManager::onHomeTileClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  lv_obj_t *cell = lv_event_get_current_target(e);
  const int offset = static_cast<int>(
      reinterpret_cast<intptr_t>(lv_obj_get_user_data(cell)));
  const std::vector<int> visible = self->visibleMenuEntries();
  const int count = static_cast<int>(visible.size());
  if (count == 0) return;
  if (offset != 0) {
    self->moveHomeSelection(offset);
    return;
  }
  if (self->homeSelection_ < 0 || self->homeSelection_ >= count) return;
  kMenuEntries[visible[static_cast<size_t>(self->homeSelection_)]].open(
      self->tabs_);
  self->render();
}

// Rotates the carousel. Wraps in both directions: with one destination per
// turn a hard stop at either end just feels broken on a knob that itself
// turns forever.
void ScreenManager::moveHomeSelection(int delta) {
  const int count = static_cast<int>(visibleMenuEntries().size());
  if (count <= 0) return;
  homeSelection_ = ((homeSelection_ + delta) % count + count) % count;
  render();
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
  lv_obj_set_style_text_font(brightnessLabel_, &knobify_text_font_28, 0);
  lv_obj_set_style_text_color(brightnessLabel_, theme::ink(), 0);
  lv_obj_align(brightnessLabel_, LV_ALIGN_TOP_MID, 0, kGlyphY + 60);

  // There's no button to press on this screen, so name the one control
  // that does something.
  lv_obj_t *hint = lv_label_create(screen_);
  lv_obj_set_style_text_font(hint, &knobify_text_font_14, 0);
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
  lv_obj_set_style_text_font(sleepValueLabel_, &knobify_text_font_28, 0);
  lv_obj_set_style_text_color(sleepValueLabel_, theme::ink(), 0);
  lv_obj_align(sleepValueLabel_, LV_ALIGN_TOP_MID, 0, kGlyphY + 60);

  lv_obj_t *hint = lv_label_create(screen_);
  lv_obj_set_style_text_font(hint, &knobify_text_font_14, 0);
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
    addLabel("Touch calibrated", &knobify_text_font_20, theme::ink(), -84);
    makeIconButton(screen_, "Keep", 96, 96, LV_ALIGN_CENTER, 0, 0,
                   &ScreenManager::onCalibrationKeepClicked, this,
                   ButtonRole::Primary, &knobify_text_font_20);
    addLabel("Reverts unless kept", &knobify_text_font_14, theme::structure(),
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
           &knobify_text_font_20, theme::ink(),
           shownCalibrationRejected_ ? -24 : -16);
  char progress[16];
  snprintf(progress, sizeof(progress), "%u of %u",
           static_cast<unsigned>(shownCalibrationTargets_ + 1),
           static_cast<unsigned>(TouchCalibrator::kTargetCount));
  addLabel(progress, &knobify_text_font_14, theme::structure(), 18);
  // The one exit, and it never depends on touch.
  addLabel("Turn knob to cancel", &knobify_text_font_14, theme::structure(),
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

void ScreenManager::startUsbDrive() {
  // The card is the computer's until the session ends: nothing here may
  // read or write it, so close the playing file and keep the sleep timer
  // from powering down mid-copy.
  playback_.stop();
  sleepTimer_.cancel();
  constexpr ui_widgets::MessageAnchor kAnchor{drivers::kLcdHorRes / 2,
                                              drivers::kLcdVerRes / 2};
  if (!usbDrive_.start(millis())) {
    messages_.show("No SD card", kAnchor, millis());
    return;
  }
  tabs_.activeStack().push(Screen{ScreenKind::UsbDrive, {}});
  render();
}

void ScreenManager::renderUsbDrive() {
  shownUsbDrivePhase_ = usbDrive_.phase();
  const bool connected = shownUsbDrivePhase_ == usbdrive::UsbDrivePhase::Connected;
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
  addLabel("USB drive", &knobify_text_font_20, theme::ink(), -96);
  addLabel(connected ? "Connected" : "Connect to a computer",
           &knobify_text_font_14, connected ? theme::accent() : theme::structure(),
           -66);
  makeIconButton(screen_, "Done", 96, 96, LV_ALIGN_CENTER, 0, 10,
                 &ScreenManager::onUsbDriveDoneClicked, this,
                 ButtonRole::Primary, &knobify_text_font_20);
  // Done doesn't wait for the computer: leaving before its writes are
  // flushed corrupts the card.
  if (connected) {
    addLabel("Eject on the computer\nbefore tapping Done",
             &knobify_text_font_14, theme::structure(), 96);
  }
}

void ScreenManager::onUsbDriveDoneClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  // Leaving the screen and the rescan happen in tickUsbDrive(), outside
  // this event (runRescan() blocks).
  self->usbDrive_.finish();
}

void ScreenManager::tickUsbDrive(uint32_t nowMs) {
  const usbdrive::UsbDrivePhase before = usbDrive_.phase();
  usbDrive_.tick(nowMs);
  if (usbDrive_.phase() != before && usbDrive_.active()) {
    Serial.printf("[usbdrive] phase %d\n", static_cast<int>(usbDrive_.phase()));
  }
  const bool onScreen =
      tabs_.activeStack().current().kind == ScreenKind::UsbDrive;
  // Left some other way (a swipe back): the card comes back too.
  if (!onScreen) usbDrive_.finish();

  if (usbDrive_.takeFinished()) {
    constexpr const char *kEndNames[] = {"none", "ejected", "host gone", "done"};
    const char *reason = kEndNames[static_cast<int>(usbDrive_.lastEnd())];
    if (onScreen) tabs_.back();
    render();
    // Every collection: the computer could have written to any of them,
    // and there is no way to tell which from here.
    runRescanAll();
    // After the rescan: the USB serial port drops with the drive, so a line
    // printed right away never reaches a monitor.
    Serial.printf("[usbdrive] session ended (%s, %s)\n", reason,
                  onScreen ? "on screen" : "screen left");
    return;
  }
  if (onScreen && renderedKind_ == ScreenKind::UsbDrive &&
      usbDrive_.phase() != shownUsbDrivePhase_) {
    render();
  }
}

}  // namespace knobify::ui
