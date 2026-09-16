#include "ScreenManager.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "BookmarkKeeper.h"
#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "PlaylistBuilder.h"
#include "ScreenHelpers.h"
#include "St77916Driver.h"
#include "Theme.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::ScreenParams;

namespace knobify::ui {

namespace {

// Updates the scan overlay's label as LibraryRescanner::rescan() walks the
// SD card -- same text/throttling as the old boot-time listener this
// replaced (main.cpp no longer scans at boot, see AGENTS.md). Uses
// lv_refr_now(), NOT lv_timer_handler(), to flush the label: rescan() runs
// synchronously from inside a list-row click (runRescan()), which is itself invoked BY an
// in-progress lv_timer_handler() call (LvglGlue::pump(), called every
// loop()) -- calling lv_timer_handler() again from in here would be a
// reentrant call into LVGL's own timer/input dispatch, which isn't
// reentrant and corrupts touch input state app-wide (found on real
// hardware 2026-09-13: taps became unreliable everywhere, not just on
// this screen, after using the scan button -- see AGENTS.md).
// lv_refr_now() only forces the pending redraw, without touching input
// devices or other timers, so it's safe to call from here.
class ScanProgressLabelListener : public knobify::library::ScanProgressListener {
 public:
  explicit ScanProgressLabelListener(lv_obj_t *label) : label_(label) {}

  void onFileScanned(size_t filesScannedSoFar) override {
    char text[48];
    snprintf(text, sizeof(text), "Scanning library...\n%u files",
             static_cast<unsigned>(filesScannedSoFar));
    lv_label_set_text(label_, text);
    // Actually flushing to the panel on every single file would slow the
    // scan down for no real benefit -- every 5th file is still clearly
    // "moving" to a human, without adding meaningful overhead.
    if (filesScannedSoFar % 5 == 0) {
      lv_refr_now(nullptr);
    }
  }

 private:
  lv_obj_t *label_;
};

}  // namespace

collection::CollectionId ScreenManager::currentCollection() const {
  return tabs_.activeStack().current().params.collection;
}

const collection::CollectionProfile &ScreenManager::profile() const {
  return collections_.profile(currentCollection());
}

library::LibraryIndex &ScreenManager::library() {
  return collections_.index(currentCollection());
}

void ScreenManager::begin() {
  uint8_t stored = 0;
  preferSpectrum_ = settings_.getU8(kSpectrumSettingKey, stored) && stored != 0;
  if (settings_.getU8(kRepeatSettingKey, stored) && stored <= 2) {
    playback_.setRepeat(static_cast<playback::RepeatMode>(stored));
  }
  loadMenuVisibility();
  render();
}

void ScreenManager::render() {
  itemContexts_.clear();
  highlightedIndex_ = 0;

  // Deleting the old screen synchronously here would free it (and the
  // button that's the current event's target, if render() was called
  // from a click handler) while LVGL is still processing that very
  // event -- corrupts LVGL's input state. lv_obj_del_async() defers the
  // actual deletion until after event processing finishes. Found on
  // real hardware 2026-09-12: tapping a list item appeared to navigate
  // forward then immediately revert, because the corrupted input state
  // led to a bogus click on whatever ended up at that freed memory
  // address in the new screen.
  lv_obj_t *oldScreen = screen_;
  screen_ = lv_obj_create(nullptr);
  // This app has its own swipe-gesture handling (GestureRecognizer) and
  // never wants a screen to scroll as a whole; a default-scrollable
  // screen only became visible as a bug once the Now Playing volume
  // ring's host was intentionally sized larger than the screen (see
  // renderNowPlaying()) -- LVGL then drew thin scrollbar lines along the
  // screen's right/bottom edges. Found on real hardware 2026-09-13.
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_scr_load(screen_);
  if (oldScreen) {
    lv_obj_del_async(oldScreen);
  }
  list_ = nullptr;
  tiles_ = nullptr;
  brightnessArcHost_ = nullptr;
  brightnessLabel_ = nullptr;
  sleepArcHost_ = nullptr;
  sleepValueLabel_ = nullptr;
  sleepTileLabel_ = nullptr;
  shownSleepMinutes_ = UINT32_MAX;
  shownSleepSeconds_ = UINT32_MAX;
  calibrationArcHost_ = nullptr;
  miniBar_ = nullptr;
  elapsedLabel_ = nullptr;
  coverImg_ = nullptr;
  spectrum_.detach();
  volumeArcHost_ = nullptr;
  volumeHudPill_ = nullptr;
  volumeHudLabel_ = nullptr;
  volumeHudVisible_ = false;
  progressArcHost_ = nullptr;
  timePill_ = nullptr;
  shuttleArcHost_ = nullptr;
  shuttleMarker_ = nullptr;
  shownShuttleHeld_ = false;
  shownShuttleStep_ = 0;
  lastShownSecond_ = -1;
  durationSeconds_ = 0;

  Screen current = tabs_.activeStack().current();
  if (current.kind != renderedKind_) {
    renderedKind_ = current.kind;
    messages_.dismissScreenMessage();
    // Leaving Now Playing closes its options panel (ADR 0014).
    optionsPanelOpen_ = false;
  }

  if (current.kind == ScreenKind::NowPlaying) {
    renderNowPlaying();
  } else if (current.kind == ScreenKind::Home) {
    renderHome();
  } else if (current.kind == ScreenKind::Brightness) {
    renderBrightness();
  } else if (current.kind == ScreenKind::SleepTimer) {
    renderSleepTimer();
  } else if (current.kind == ScreenKind::UsbDrive) {
    // Modal: no back button or caption. Done, eject or unplug end it.
    renderUsbDrive();
    return;
  } else if (current.kind == ScreenKind::TouchCalibration) {
    // No back button or caption: the top target sits where they would,
    // and the knob is the way out (renderTouchCalibration()).
    renderTouchCalibration();
    return;
  } else {
    std::vector<std::pair<std::string, int>> items;
    // Library lists start with a Shuffle row whose scope is the list itself:
    // the whole library, this artist, this album (ADR 0011). No scope
    // setting -- where you start is the scope.
    if (hasShuffleRow(current.kind)) {
      items.emplace_back(LV_SYMBOL_SHUFFLE "  Shuffle", kShuffleItemId);
    }
    // A spoken-word title you are part-way through leads with Continue,
    // in the slot Shuffle occupies for music (ADR 0018). Tapping a part
    // still plays that part from its start -- an explicit choice is never
    // overridden by a remembered position.
    resume::Bookmark bookmark;
    if (hasContinueRow(current, bookmark)) {
      items.emplace_back(LV_SYMBOL_PLAY "  Continue", kContinueItemId);
    }
    switch (current.kind) {
      case ScreenKind::Artists:
        // In the collection's own order, like the tap handler below --
        // both go through artistsSorted() so a row's index means the same
        // thing in each.
        for (auto artistId : library().artistsSorted(profile().sort)) {
          items.emplace_back(library().artists[artistId].name,
                             static_cast<int>(artistId));
        }
        break;
      case ScreenKind::Albums:
        for (auto albumId :
             library().albumsFor(current.params.artistId, profile().sort)) {
          for (const auto &album : library().albums) {
            if (album.id == albumId) items.emplace_back(album.title, albumId);
          }
        }
        break;
      case ScreenKind::Tracks:
        for (auto trackId : library().tracksFor(current.params.albumId)) {
          for (const auto &track : library().tracks) {
            if (track.id == trackId) items.emplace_back(track.title, trackId);
          }
        }
        break;
      case ScreenKind::Folder: {
        auto entries = library::FolderBrowser::list(
            directoryReader_, current.params.folderPath);
        for (size_t i = 0; i < entries.size(); ++i) {
          std::string label =
              entries[i].isDirectory ? entries[i].name + "/" : entries[i].name;
          items.emplace_back(label, static_cast<int>(i));
        }
        break;
      }
      case ScreenKind::Settings:
        for (int i = 0; i < kSettingsRowCount; ++i) {
          items.emplace_back(kSettingsRows[i].label, i);
        }
        break;
      case ScreenKind::RescanPicker:
        // One row per collection, then "All" -- so the row index is the
        // collection's own value, and one past it means every collection.
        for (const auto &collectionProfile : collection::kCollections) {
          items.emplace_back(
              collectionProfile.label,
              static_cast<int>(collection::indexOf(collectionProfile.id)));
        }
        items.emplace_back("All", static_cast<int>(collection::kCollectionCount));
        break;
      case ScreenKind::MenuVisibility:
        appendMenuVisibilityRows(items);
        break;
      default:
        break;
    }
    renderList(items, playback_.state() != playback::PlaybackState::Stopped);
  }

  // Created last (on top of the list widget, which otherwise spans the
  // whole screen and would draw over -- and steal taps from -- a button
  // created earlier at the same top-center position). Found on real
  // hardware 2026-09-12: the back button worked on Now Playing (no
  // full-screen widget there) but was invisible/unclickable on every
  // list screen.
  renderBackButtonIfNeeded();
  renderContextCaption();
}

void ScreenManager::renderList(
    const std::vector<std::pair<std::string, int>> &items, bool showMiniBar) {
  Screen current = tabs_.activeStack().current();
  // Rows that are actions rather than data (Shuffle, Continue) sit at the
  // top, so every row below them is one further along than its index.
  int leadingRows = 0;
  for (const auto &item : items) {
    if (item.second != kShuffleItemId && item.second != kContinueItemId) break;
    ++leadingRows;
  }
  list_ = lv_list_create(screen_);
  // Starts below the header zone (back button + context caption)
  // and ends above the mini-bar, rather than spanning the whole screen
  // with top padding -- rows used to scroll underneath the fixed back
  // button and visibly collide with it (ux-guidelines §7).
  lv_coord_t listHeight = drivers::kLcdVerRes - kListTopY -
                          (showMiniBar ? kMiniBarZoneHeight : 0);
  lv_obj_set_size(list_, drivers::kLcdHorRes, listHeight);
  lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, kListTopY);
  // The default scrollbar sits flush against the right edge, which on a
  // round display gets clipped by the bezel there (looked like a
  // stray pink line, cut off mid-stroke) -- the highlighted item already
  // shows position for knob-driven scrolling, so the scrollbar itself
  // isn't needed.
  lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
  // Symmetric insets: the selected row's filled background shows both
  // ends, and at the first row's height (y=74) the bezel only shows
  // x~35..325 (ux-guidelines §7).
  lv_obj_set_style_pad_left(list_, 36, 0);
  lv_obj_set_style_pad_right(list_, 36, 0);
  lv_obj_set_style_pad_top(list_, 0, 0);
  // Without a mini-bar the list runs to the bottom edge, where the round
  // bezel narrows sharply -- extra bottom padding lets the last rows
  // scroll up into the readable area.
  lv_obj_set_style_pad_bottom(list_, showMiniBar ? 8 : 48, 0);

  std::vector<library::FolderEntry> folderEntries;
  if (current.kind == ScreenKind::Folder) {
    folderEntries =
        library::FolderBrowser::list(directoryReader_, current.params.folderPath);
  }

  // A multi-disc album's rows show "disc-track" (tracks restart per disc);
  // a single-disc album's just the track number.
  bool multiDisc = false;
  if (current.kind == ScreenKind::Tracks) {
    uint16_t firstDisc = 0;
    for (const auto &track : library().tracks) {
      if (track.albumId != current.params.albumId) continue;
      uint16_t disc = track.discNumber == 0 ? 1 : track.discNumber;
      if (firstDisc == 0) {
        firstDisc = disc;
      } else if (disc != firstDisc) {
        multiDisc = true;
        break;
      }
    }
  }

  for (size_t i = 0; i < items.size(); ++i) {
    lv_obj_t *btn = lv_list_add_btn(list_, nullptr, items[i].first.c_str());
    // Checkable + the theme's own checked style (rather than a manual
    // bg_color override) keeps text contrast correct for free -- see
    // applyHighlight() and Theme.cpp's row styles.
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_pad_top(btn, 10, 0);
    lv_obj_set_style_pad_bottom(btn, 10, 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 8, 0);

    // lv_list_add_btn's label defaults to a scrolling marquee for long
    // text and the small default font -- a bigger font plus a clean
    // ellipsis truncation reads better on a small round display than
    // several artist/album names all mid-scroll at once.
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
      lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    }

    // Album rows end in the release year -- albums are sorted by it
    // (LibraryIndex::albumsFor()), so this makes the order legible. Plain
    // text, no pill/badge (ux-guidelines §7), and nothing at all when the
    // year is unknown. Colored per row state by applyHighlight().
    if (current.kind == ScreenKind::Albums) {
      for (const auto &album : library().albums) {
        if (album.id != static_cast<library::AlbumId>(items[i].second) ||
            album.year == 0) {
          continue;
        }
        char yearText[8];
        snprintf(yearText, sizeof(yearText), "%u",
                 static_cast<unsigned>(album.year));
        lv_obj_t *yearLabel = lv_label_create(btn);
        lv_obj_set_style_text_font(yearLabel, &lv_font_montserrat_14, 0);
        lv_label_set_text(yearLabel, yearText);
        lv_obj_set_style_pad_column(btn, 10, 0);
        break;
      }
    }

    // Track rows end in their track number, styled like the album year --
    // tracks are sorted by it (LibraryIndex::tracksFor()). Nothing when
    // the number is unknown.
    if (current.kind == ScreenKind::Tracks &&
        items[i].second != kShuffleItemId &&
        items[i].second != kContinueItemId) {
      const auto trackId = static_cast<library::TrackId>(items[i].second);
      if (trackId < library().tracks.size() &&
          library().tracks[trackId].trackNumber != 0) {
        const library::Track &track = library().tracks[trackId];
        char numberText[12];
        if (multiDisc) {
          snprintf(numberText, sizeof(numberText), "%u-%02u",
                   static_cast<unsigned>(
                       track.discNumber == 0 ? 1 : track.discNumber),
                   static_cast<unsigned>(track.trackNumber));
        } else {
          snprintf(numberText, sizeof(numberText), "%u",
                   static_cast<unsigned>(track.trackNumber));
        }
        lv_obj_t *numberLabel = lv_label_create(btn);
        lv_obj_set_style_text_font(numberLabel, &lv_font_montserrat_14, 0);
        lv_label_set_text(numberLabel, numberText);
        lv_obj_set_style_pad_column(btn, 10, 0);
      }
    }

    // The Brightness row ends in its current value, styled like the album
    // year: a plain secondary fact, not a badge.
    char valueText[8] = {0};
    const char *secondary = nullptr;
    if (current.kind == ScreenKind::MenuVisibility) {
      // "On"/"Off"/"Always" -- the row's own state, read the same way the
      // Brightness row's percentage is (ADR 0018).
      secondary = menuVisibilityValue(items[i].second);
    } else if (current.kind == ScreenKind::Settings && items[i].second == 0) {
      snprintf(valueText, sizeof(valueText), "%u%%",
               static_cast<unsigned>(brightness_.percent()));
      secondary = valueText;
    }
    if (secondary) {
      lv_obj_t *valueLabel = lv_label_create(btn);
      lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_14, 0);
      lv_label_set_text(valueLabel, secondary);
      lv_obj_set_style_pad_column(btn, 10, 0);
    }

    // After the year/number label exists: the title label flex-grows into
    // whatever width the year leaves, and truncates within that.
    if (label) setClampedText(label, items[i].first.c_str(), 1);

    auto ctx = std::make_unique<ItemContext>();
    ctx->self = this;
    ctx->isShuffle = items[i].second == kShuffleItemId;
    ctx->isContinue = items[i].second == kContinueItemId;
    // Index into the screen's data (artists, albums, tracks), not the row
    // -- so a leading Shuffle or Continue row doesn't shift everything.
    ctx->index = static_cast<int>(i) - (leadingRows > 0 ? leadingRows : 0);
    ctx->albumId = current.params.albumId;
    if (current.kind == ScreenKind::Tracks && !ctx->isShuffle) {
      ctx->trackId = static_cast<library::TrackId>(items[i].second);
    }
    if (current.kind == ScreenKind::Folder && i < folderEntries.size()) {
      ctx->isFolder = folderEntries[i].isDirectory;
      ctx->path = current.params.folderPath;
      if (ctx->path.back() != '/') ctx->path += '/';
      ctx->path += folderEntries[i].name;
    } else {
      ctx->isFolder = false;
    }
    itemContexts_.push_back(std::move(ctx));

    lv_obj_add_event_cb(btn, &ScreenManager::onListItemClicked,
                         LV_EVENT_CLICKED, itemContexts_.back().get());
  }

  if (showMiniBar) renderMiniBar();

  applyHighlight();
}

void ScreenManager::renderMiniBar() {
  // A light-grey bottom area rather than a floating pill -- Braun
  // keeps surfaces neutral and puts color only on small functional
  // details, so only the playback glyph is green ("active/running",
  // ux-guidelines §3). A pale green tint was tried first (2026-09-13). Full-width and flush
  // with the bottom edge on purpose: the round bezel cuts it into a
  // circle segment that echoes the device's shape. (Earlier attempts: a
  // bordered white box read as a text input, an ink pill as a second
  // selected row, accent/grey as too loud/too anonymous -- user
  // feedback 2026-09-12/13.) Its content stays narrow and near the top
  // of the area, where the segment is still wide.
  miniBar_ = lv_obj_create(screen_);
  lv_obj_set_size(miniBar_, drivers::kLcdHorRes, kMiniBarZoneHeight);
  lv_obj_align(miniBar_, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(miniBar_, 0, 0);
  lv_obj_set_style_bg_color(miniBar_, theme::surfaceAlt(), 0);
  lv_obj_set_style_border_width(miniBar_, 0, 0);
  lv_obj_set_style_pad_all(miniBar_, 0, 0);
  // A long title otherwise made the bar itself scrollable, showing a
  // scrollbar inside it.
  lv_obj_clear_flag(miniBar_, LV_OBJ_FLAG_SCROLLABLE);

  // Glyph + title as one horizontally centered row, so a short title
  // stays centered too. At the text's height (~y=294..314) the bezel
  // still shows ~220px, so the row is capped below that.
  lv_obj_t *row = lv_obj_create(miniBar_);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 22);

  // Playback *state*, not an action -- tapping the whole area opens
  // Now Playing.
  lv_obj_t *stateGlyph = lv_label_create(row);
  lv_label_set_text(stateGlyph,
                    playback_.state() == playback::PlaybackState::Playing
                        ? LV_SYMBOL_PLAY
                        : LV_SYMBOL_PAUSE);
  lv_obj_set_style_text_color(stateGlyph, theme::confirm(), 0);

  constexpr lv_coord_t kMaxTitleWidth = 170;
  const std::string title = trackInfoFor(playback_.currentPath()).title;
  lv_obj_t *label = lv_label_create(row);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, theme::ink(), 0);
  lv_label_set_text(label, title.c_str());
  lv_obj_update_layout(label);
  if (lv_obj_get_width(label) > kMaxTitleWidth) {
    lv_obj_set_width(label, kMaxTitleWidth);
    setClampedText(label, title.c_str(), 1);
  }

  lv_obj_add_flag(miniBar_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(miniBar_, &ScreenManager::onMiniBarClicked,
                       LV_EVENT_CLICKED, this);
}

void ScreenManager::renderContextCaption() {
  // Tells you where you are in the hierarchy -- the top of a round screen
  // is too narrow to be useful for list rows anyway (ux-guidelines §7).
  Screen current = tabs_.activeStack().current();
  std::string caption;
  switch (current.kind) {
    case ScreenKind::Artists:
      // The collection's own name -- with three of them, "Library" no
      // longer says which one you are in (ADR 0018).
      caption = profile().label;
      break;
    case ScreenKind::Settings:
      caption = "Settings";
      break;
    case ScreenKind::RescanPicker:
      caption = "Rescan";
      break;
    case ScreenKind::MenuVisibility:
      caption = "Main menu";
      break;
    case ScreenKind::Brightness:
      caption = "Brightness";
      break;
    case ScreenKind::SleepTimer:
      caption = "Sleep timer";
      break;
    case ScreenKind::Albums:
      for (const auto &artist : library().artists) {
        if (artist.id == current.params.artistId) caption = artist.name;
      }
      break;
    case ScreenKind::Tracks:
      for (const auto &album : library().albums) {
        if (album.id == current.params.albumId) caption = album.title;
      }
      break;
    case ScreenKind::Folder: {
      std::string path = current.params.folderPath;
      while (path.size() > 1 && path.back() == '/') path.pop_back();
      auto slash = path.find_last_of('/');
      caption = (path.empty() || path == "/")
                    ? "Files"
                    : (slash == std::string::npos ? path : path.substr(slash + 1));
      if (path == profile().rootPath) caption = profile().label;
      break;
    }
    default:
      return;
  }
  lv_obj_t *label = lv_label_create(screen_);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, theme::structure(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(label, 200);
  setClampedText(label, caption.c_str(), 1);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, kCaptionY);
}

void ScreenManager::renderBackButtonIfNeeded() {
  // Swiping left-to-right also goes back (decision 5, ADR 0004), but
  // real usage showed that's not discoverable on its own -- a user
  // reaching Now Playing had no visible way back at all. This is a
  // supplementary, always-visible affordance for the same action.
  // Positioned top-center rather than a corner: the round bezel clips
  // corners much more aggressively than top-center at this height.
  // On a music tab's root there's nothing to pop, so it leads back to the
  // main menu instead (ADR 0010) -- still a left chevron, since the
  // caption names where you are.
  if (!tabs_.canGoBackOrHome()) return;
  // On Now Playing a left chevron read like "previous track" (right
  // above the previous button) and didn't say where it goes -- a down
  // chevron instead means "collapse the player" back into the list's
  // mini-bar, which is exactly what popping this screen does
  // (ux-guidelines §5).
  const char *glyph =
      tabs_.activeStack().current().kind == ScreenKind::NowPlaying
          ? LV_SYMBOL_DOWN
          : LV_SYMBOL_LEFT;
  makeIconButton(screen_, glyph, kHeaderButtonW, kHeaderButtonH,
                 LV_ALIGN_TOP_MID, 0, kHeaderButtonY,
                 &ScreenManager::onBackClicked, this, ButtonRole::Quiet,
                 &lv_font_montserrat_20);
}

ScreenManager::TrackInfo ScreenManager::trackInfoFor(
    const std::string &path) const {
  // Tags, not filenames (ux-guidelines §7) -- "08 Seeing Out the Angel"
  // is a filename, "Seeing Out the Angel" is the song.
  TrackInfo info;
  for (const auto &track : library().tracks) {
    if (track.filePath != path) continue;
    info.title = track.title;
    for (const auto &album : library().albums) {
      if (album.id != track.albumId) continue;
      info.album = album.title;
      for (const auto &artist : library().artists) {
        if (artist.id == album.artistId) info.artist = artist.name;
      }
    }
    break;
  }
  if (info.title.empty()) info.title = friendlyName(path);
  return info;
}

void ScreenManager::renderNowPlaying() {
  // Layout, top to bottom (y = top edge within the 360px frame; see
  // docs/design/ux-guidelines.md §3a/§7): back button, cover, title,
  // artist/album line, transport row, time, lock button. Every element
  // is centered horizontally -- corners are clipped by the round bezel.
  //
  // Cover slot: the album cover or the dot-matrix spectrum (ADR 0009),
  // switched by tapping it. No cover cached for this album -> the spectrum
  // takes the slot; it's live data, not a placeholder, so the layout below
  // is the same either way.
  uint16_t coverSize = 0;
  std::string albumFolderPath =
      library::CoverArtCache::albumFolderPathFor(playback_.currentPath());
  if (!coverReader_.loadCover(albumFolderPath, &coverSize, &coverPixels_)) {
    // Not cached yet -- generate it now, lazily, from this one already-
    // isolated file open (not preceded by any SD directory walk). Found
    // on real hardware 2026-09-13: generating covers in a tight batch
    // right after a full-library directory walk reliably wedges the
    // SD_MMC controller (every subsequent open fails) even though a
    // single isolated open like this one -- the same one playback
    // itself just did -- works reliably. See AGENTS.md.
    auto file = fileOpener_.open(playback_.currentPath());
    if (file) {
      auto tags = library::TagReader::read(*file, playback_.currentPath());
      library::CoverArtCache::ensureCoverCached(albumFolderPath, *file, tags,
                                                  directoryReader_, fileOpener_,
                                                  jpegDecoder_, coverWriter_);
      coverReader_.loadCover(albumFolderPath, &coverSize, &coverPixels_);
    }
  }
  if (coverSize != 0) {
    coverImgDsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    coverImgDsc_.header.always_zero = 0;
    coverImgDsc_.header.w = coverSize;
    coverImgDsc_.header.h = coverSize;
    coverImgDsc_.data_size =
        static_cast<uint32_t>(coverPixels_.size() * sizeof(uint16_t));
    coverImgDsc_.data = reinterpret_cast<const uint8_t *>(coverPixels_.data());

    coverImg_ = lv_img_create(screen_);
    lv_img_set_src(coverImg_, &coverImgDsc_);
    lv_obj_align(coverImg_, LV_ALIGN_TOP_MID, 0, kCoverY);
  } else {
    coverImg_ = nullptr;
    coverPixels_.clear();
  }

  static_assert(ui_widgets::DotMatrixSpectrum::kSize ==
                    library::CoverArtCache::kCoverSize,
                "the spectrum replaces the cover in the same slot");
  lv_obj_t *spectrum = spectrum_.create(screen_, theme::ink(),
                                        theme::surfaceAlt(), theme::surface());
  lv_obj_align(spectrum, LV_ALIGN_TOP_MID, 0, kCoverY);
  // Switched from the options panel (ADR 0014), no longer by tapping the
  // slot itself: nothing on screen said the cover was tappable.
  applyCoverSlotMode();
  lastSpectrumTickMs_ = millis();

  const lv_coord_t slotSize = ui_widgets::DotMatrixSpectrum::kSize;
  lv_coord_t titleY = kCoverY + slotSize + 10;
  TrackInfo info = trackInfoFor(playback_.currentPath());

  lv_obj_t *title = lv_label_create(screen_);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  // Wide: without a cover this block sits near the vertical middle, where
  // the round screen is ~330px across; with a cover (y~164) still ~320px.
  lv_obj_set_width(title, 280);
  // One line: the cover slot (cover or spectrum) is always there, and the
  // transport row leaves no space for a second line. The artist line
  // follows the title's actual height rather than a fixed offset.
  setClampedText(title, info.title.c_str(), 1);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, titleY);
  lv_coord_t metaY = titleY + lv_obj_get_height(title) + 4;

  std::string meta = info.artist;
  if (!info.album.empty()) {
    // Plain ASCII: LVGL's built-in Montserrat only covers 0x20-0x7F (plus
    // LV_SYMBOL_*), so a middle dot rendered as a missing-glyph box.
    if (!meta.empty()) meta += " - ";
    meta += info.album;
  }
  if (!meta.empty()) {
    lv_obj_t *metaLabel = lv_label_create(screen_);
    lv_obj_set_style_text_font(metaLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(metaLabel, theme::structure(), 0);
    lv_obj_set_style_text_align(metaLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(metaLabel, 300);
    setClampedText(metaLabel, meta.c_str(), 1);
    lv_obj_align(metaLabel, LV_ALIGN_TOP_MID, 0, metaY);
  }

  // Song-progress ring -- the resting-state edge ring on this screen.
  // Braun Yellow, like the second hand of a Braun clock: time passing.
  // Anthracite (tried first) blended into the dark housing right next to
  // the bezel (user feedback 2026-09-13), and orange stays reserved for
  // the Play/Pause action (ux-guidelines §3). Hidden
  // until updateElapsedTimeDisplay() knows the track's duration.
  ui_widgets::EdgeArcConfig progressArcConfig;
  progressArcConfig.startAngle = 135;
  progressArcConfig.endAngle = 45;
  progressArcConfig.widthPx = 9;
  progressArcConfig.color = theme::time();
  progressArcConfig.hasBackgroundColor = true;
  progressArcConfig.backgroundColor = theme::surfaceAlt();
  progressArcHost_ = makeEdgeArcHost(screen_);
  progressArc_.create(progressArcHost_, progressArcConfig, 0, 1000);
  progressArc_.setValue(0.0f);
  lv_obj_add_flag(progressArcHost_, LV_OBJ_FLAG_HIDDEN);

  // Shuttle indicator (ADR 0013), only while the time pill is held: an ink
  // tick at the top marks normal speed, and a thin ink arc just inside the
  // progress ring grows from it -- clockwise forward, counterclockwise
  // back, 24° per step. The one deliberate exception to "one edge ring at
  // a time": speed and position are both needed while scrubbing. Ink, not
  // a signal color: it sits on the surface, not against the housing, and
  // yellow/orange/green already mean something.
  constexpr lv_coord_t kShuttleArcWidth = 5;
  constexpr lv_coord_t kShuttleArcGap = 3;
  ui_widgets::EdgeArcConfig shuttleArcConfig;
  shuttleArcConfig.startAngle = 150;
  shuttleArcConfig.endAngle = 30;
  shuttleArcConfig.widthPx = kShuttleArcWidth;
  shuttleArcConfig.color = theme::ink();
  shuttleArcConfig.mode = LV_ARC_MODE_SYMMETRICAL;
  shuttleArcHost_ = makeEdgeArcHost(screen_, progressArcConfig.widthPx + kShuttleArcGap);
  shuttleArc_.create(shuttleArcHost_, shuttleArcConfig, -playback::Shuttle::kMaxStep,
                     playback::Shuttle::kMaxStep);
  shuttleArc_.setValue(static_cast<int32_t>(0));
  // No track behind the arc: only the speed is drawn.
  lv_obj_set_style_arc_opa(shuttleArc_.raw(), LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);

  // Same host as the progress ring, so it lines up with it wherever that
  // ring actually lands on the bezel.
  shuttleMarker_ = lv_obj_create(progressArcHost_);
  lv_obj_set_size(shuttleMarker_, 4,
                  progressArcConfig.widthPx + kShuttleArcGap + kShuttleArcWidth);
  lv_obj_align(shuttleMarker_, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(shuttleMarker_, theme::ink(), 0);
  lv_obj_set_style_border_width(shuttleMarker_, 0, 0);
  lv_obj_set_style_radius(shuttleMarker_, 0, 0);
  lv_obj_set_style_pad_all(shuttleMarker_, 0, 0);
  lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);

  // Round-edge volume HUD: a ring flush against the physical bezel plus a
  // numeric readout, hidden until the first adjustment;
  // updateVolumeDisplay()/tickVolumeHud() show it and auto-hide it after
  // kVolumeHudTimeoutMs, like a phone's volume overlay -- replaces the
  // always-on linear bar (removed per user feedback 2026-09-13: it never
  // updated live, and permanently occupied center screen for a value
  // that's rarely being actively watched). Accent orange: actively being
  // set, and bright against the dark housing (ink, tried first, blended
  // into it). Deliberately not the progress ring's yellow -- the two
  // replace each other in the same place, and one color would make a
  // volume change look like the song position jumped.
  ui_widgets::EdgeArcConfig volumeArcConfig;
  volumeArcConfig.startAngle = 135;
  volumeArcConfig.endAngle = 45;
  volumeArcConfig.widthPx = 12;
  volumeArcConfig.color = theme::accent();
  volumeArcConfig.hasBackgroundColor = true;
  volumeArcConfig.backgroundColor = theme::surfaceAlt();
  volumeArcHost_ = makeEdgeArcHost(screen_);
  volumeArc_.create(volumeArcHost_, volumeArcConfig,
                     playback::PlaybackStateMachine::kMinVolume,
                     playback::PlaybackStateMachine::kMaxVolume);
  volumeArc_.setValue(static_cast<int32_t>(playback_.volume()));
  lv_obj_add_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);

  // Numeric readout in an ink pill over the cover slot's center --
  // readable on top of any cover image or the spectrum, and created after
  // them so LVGL's creation-order z-stacking draws it on top.
  volumeHudPill_ = lv_obj_create(screen_);
  lv_obj_set_size(volumeHudPill_, 72, 44);
  lv_coord_t pillY = kCoverY + slotSize / 2 - 22;
  lv_obj_align(volumeHudPill_, LV_ALIGN_TOP_MID, 0, pillY);
  lv_obj_set_style_radius(volumeHudPill_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(volumeHudPill_, theme::ink(), 0);
  lv_obj_set_style_border_width(volumeHudPill_, 0, 0);
  lv_obj_set_style_pad_all(volumeHudPill_, 0, 0);
  lv_obj_clear_flag(volumeHudPill_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(volumeHudPill_, LV_OBJ_FLAG_CLICKABLE);
  volumeHudLabel_ = lv_label_create(volumeHudPill_);
  lv_obj_set_style_text_font(volumeHudLabel_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(volumeHudLabel_, theme::surface(), 0);
  char volText[8];
  snprintf(volText, sizeof(volText), "%d", playback_.volume());
  lv_label_set_text(volumeHudLabel_, volText);
  lv_obj_center(volumeHudLabel_);
  lv_obj_add_flag(volumeHudPill_, LV_OBJ_FLAG_HIDDEN);

  // Options handle -- bottom-center, mirroring the back chevron at the top.
  // Shuffle, repeat, cover/spectrum and lock moved behind it (ADR 0014):
  // play, song and position stayed on the screen, and the three stacked
  // rows below the title (transport, time, lock) had no gap left between
  // them (user feedback 2026-09-15). Created before the time pill so the
  // pill wins where the handle's slop reaches up to it.
  //
  // An ellipsis, not an up chevron (user feedback 2026-09-16): a chevron
  // promises a direction -- and this one sat at the bottom of a screen
  // whose back control is a *down* chevron, so the pair read as a
  // contradiction. "..." is the conventional "more options" mark and says
  // what is behind it rather than which way it moves.
  makeIconButton(screen_, "...", kHeaderButtonW, kOptionsHandleH,
                 LV_ALIGN_BOTTOM_MID, 0, -kOptionsHandleBottom,
                 &ScreenManager::onOptionsHandleClicked, this,
                 ButtonRole::Quiet, &lv_font_montserrat_20);

  // Transport row: secondary (grey) prev/next either side of the one
  // primary control. Inset well within the round display's visible area at this
  // height -- see decision 6, ADR 0004.
  makeIconButton(screen_, LV_SYMBOL_PREV, 56, 56, LV_ALIGN_TOP_MID, -84,
                 kTransportCenterY - 28, &ScreenManager::onPrevClicked, this,
                 ButtonRole::Secondary, &lv_font_montserrat_20);

  makeIconButton(screen_,
                 playback_.state() == playback::PlaybackState::Playing
                     ? LV_SYMBOL_PAUSE
                     : LV_SYMBOL_PLAY,
                 72, 72, LV_ALIGN_TOP_MID, 0, kTransportCenterY - 36,
                 &ScreenManager::onPlayPauseClicked, this, ButtonRole::Primary,
                 &lv_font_montserrat_28);

  makeIconButton(screen_, LV_SYMBOL_NEXT, 56, 56, LV_ALIGN_TOP_MID, 84,
                 kTransportCenterY - 28, &ScreenManager::onNextClicked, this,
                 ButtonRole::Secondary, &lv_font_montserrat_20);

  // Elapsed (and, once known, total) play time -- requested after real
  // hardware testing made it clear there was no way to tell whether
  // playback was actually progressing. Wall-clock time since the track
  // started minus paused time (see PlaybackStateMachine::elapsedMs()),
  // not the decoder's own position -- close enough for a simple readout.
  const lv_coord_t timeY = kTimePillY + 6;
  if (playback_.canSeek()) {
    // Hold-and-turn shuttle (ADR 0013): the readout of the song position
    // is the control that changes it. Fast-wind marks say it can be
    // held; no marks (Ogg) means it can't. No extended hit area -- Play
    // and the options handle sit close above and below.
    timePill_ = makeHoldButton(screen_, "0:00", kTimePillW, kTimePillH,
                               LV_ALIGN_TOP_MID, 0, kTimePillY,
                               &ScreenManager::onTimePillPressed,
                               &ScreenManager::onTimePillReleased, this,
                               ButtonRole::Secondary, &knobify_icon_font_16);
    lv_obj_set_ext_click_area(timePill_, 0);
    elapsedLabel_ = lv_obj_get_child(timePill_, 0);
  } else {
    elapsedLabel_ = lv_label_create(screen_);
    lv_obj_set_style_text_font(elapsedLabel_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(elapsedLabel_, theme::structure(), 0);
    lv_obj_align(elapsedLabel_, LV_ALIGN_TOP_MID, 0, timeY);
    lv_label_set_text(elapsedLabel_, "0:00");
  }
  updateElapsedTimeDisplay();

  // Last, so it stacks above everything else on this screen.
  if (optionsPanelOpen_) renderOptionsPanel(animateOptionsPanel_);
  animateOptionsPanel_ = false;
}

// The options panel (ADR 0014): a sheet sliding up over the lower half of
// Now Playing with the controls used less often. A scrim under it takes
// taps outside the sheet, so closing it never also hits Play or the pill.
// Rebuilt open on every render() (a toggle re-renders the screen), only
// animated when it's opened.
void ScreenManager::renderOptionsPanel(bool animate) {
  lv_obj_t *scrim = lv_obj_create(screen_);
  lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
  lv_obj_center(scrim);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  // The theme's default padding would shift the sheet right (seen on the
  // device 2026-09-15) -- it is placed in screen coordinates.
  lv_obj_set_style_pad_all(scrim, 0, 0);
  lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scrim, &ScreenManager::onOptionsPanelCloseClicked,
                      LV_EVENT_CLICKED, this);

  lv_obj_t *panel = lv_obj_create(scrim);
  lv_obj_set_size(panel, drivers::kLcdHorRes, drivers::kLcdVerRes - kOptionsPanelY);
  lv_obj_set_pos(panel, 0, kOptionsPanelY);
  lv_obj_set_style_bg_color(panel, theme::surface(), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_set_style_pad_all(panel, 0, 0);
  // A hairline edge instead of a shadow: flat, like the rest of the design.
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(panel, theme::surfaceAlt(), 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  // Taps on the sheet's empty space must not reach the scrim and close it.
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);

  makeIconButton(panel, LV_SYMBOL_DOWN, kHeaderButtonW, kOptionsHandleH,
                 LV_ALIGN_TOP_MID, 0, 4,
                 &ScreenManager::onOptionsPanelCloseClicked, this,
                 ButtonRole::Quiet, &lv_font_montserrat_20);

  // Four secondary circles in a row, each named underneath -- the icons
  // alone didn't say what shuffle/repeat were doing (ADR 0011), and the
  // cover switch had no icon at all before. Active state is the glyph in
  // confirm green, as the toggles had on Now Playing.
  constexpr lv_coord_t kButtonSize = 64;
  constexpr lv_coord_t kPitch = 72;
  constexpr lv_coord_t kButtonTop = 60;
  struct Option {
    const char *glyph;
    const char *label;
    bool active;
    bool enabled;
    lv_event_cb_t cb;
  };
  playback::RepeatMode repeat = playback_.repeat();
  bool showingSpectrum = preferSpectrum_ || !coverImg_;
  const Option options[] = {
      {KNOBIFY_ICON_SHUFFLE, "Shuffle", playback_.shuffle(), true,
       &ScreenManager::onShuffleClicked},
      {repeat == playback::RepeatMode::One ? KNOBIFY_ICON_REPEAT_ONE
                                           : KNOBIFY_ICON_REPEAT,
       "Repeat", repeat != playback::RepeatMode::Off, true,
       &ScreenManager::onRepeatClicked},
      // Shows what a tap switches to; only switchable when there is a cover.
      {showingSpectrum ? KNOBIFY_ICON_IMAGE : KNOBIFY_ICON_EQUALIZER,
       showingSpectrum ? "Cover" : "Spectrum", false, coverImg_ != nullptr,
       &ScreenManager::onCoverSwitchClicked},
      {KNOBIFY_ICON_LOCK, "Lock", false, true, &ScreenManager::onLockClicked},
  };
  constexpr int kCount = sizeof(options) / sizeof(options[0]);
  for (int i = 0; i < kCount; ++i) {
    const Option &option = options[i];
    lv_coord_t x = static_cast<lv_coord_t>((2 * i - (kCount - 1)) * kPitch / 2);
    lv_obj_t *btn = makeIconButton(panel, option.glyph, kButtonSize, kButtonSize,
                                   LV_ALIGN_TOP_MID, x, kButtonTop, option.cb,
                                   this, ButtonRole::Secondary,
                                   &knobify_icon_font_28);
    lv_obj_set_ext_click_area(btn, 0);
    lv_obj_t *label = lv_label_create(panel);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, theme::structure(), 0);
    lv_label_set_text(label, option.label);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x, kButtonTop + kButtonSize + 6);
    if (option.active) {
      lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), theme::confirm(), 0);
    }
    if (!option.enabled) {
      lv_obj_add_state(btn, LV_STATE_DISABLED);
      lv_obj_set_style_opa(btn, LV_OPA_40, 0);
      lv_obj_set_style_opa(label, LV_OPA_40, 0);
    }
  }

  if (animate) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, panel);
    lv_anim_set_values(&anim, drivers::kLcdVerRes, kOptionsPanelY);
    lv_anim_set_time(&anim, kOptionsPanelAnimMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void *obj, int32_t y) {
      lv_obj_set_y(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(y));
    });
    lv_anim_start(&anim);
  }
}

void ScreenManager::applyHighlight() {
  // Home has no highlight to move: the carousel's selection decides which
  // entries are on screen at all, so moveHomeSelection() re-renders (ADR
  // 0018) rather than restyling existing children.
  if (tiles_) return;
  if (!list_) return;
  // Toggling LV_STATE_CHECKED and letting the theme render it (rather
  // than overriding bg_color by hand) guarantees the theme's own
  // contrast-correct text/background pairing for both states.
  uint32_t count = lv_obj_get_child_cnt(list_);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t *btn = lv_obj_get_child(list_, i);
    bool selected = static_cast<int>(i) == highlightedIndex_;
    if (selected) {
      lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
    // Trailing value labels (album year, brightness percent) after the
    // title: inheriting the row color at reduced opacity came out as dark
    // grey on the ink selected row, barely readable (user feedback
    // 2026-09-15) -- so light grey there, mid anthracite otherwise.
    uint32_t parts = lv_obj_get_child_cnt(btn);
    for (uint32_t c = 1; c < parts; ++c) {
      lv_obj_t *value = lv_obj_get_child(btn, c);
      if (!lv_obj_check_type(value, &lv_label_class)) continue;
      lv_obj_set_style_text_color(
          value, selected ? theme::surfaceAlt() : theme::structure(), 0);
    }
  }
}

void ScreenManager::applyCoverSlotMode() {
  bool showSpectrum = preferSpectrum_ || !coverImg_;
  if (coverImg_) {
    if (showSpectrum) {
      lv_obj_add_flag(coverImg_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(coverImg_, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (!spectrum_.raw()) return;
  if (showSpectrum) {
    lv_obj_clear_flag(spectrum_.raw(), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(spectrum_.raw(), LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::tickSpectrum(uint32_t nowMs, bool visible) {
  lv_obj_t *canvas = spectrum_.raw();
  if (!canvas || lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN)) return;
  if (!visible) {
    lastSpectrumTickMs_ = nowMs;
    return;
  }
  uint32_t dtMs = nowMs - lastSpectrumTickMs_;
  if (dtMs < kSpectrumFrameMs) return;
  lastSpectrumTickMs_ = nowMs;
  if (dtMs > 100) dtMs = 100;  // After a stall, don't jump straight to empty.

#ifdef KNOBIFY_SPECTRUM_DEBUG
  int64_t startUs = esp_timer_get_time();
#endif
  playback::SampleWindow window = playback_.readRecentSamples(
      spectrumSamples_.data(), spectrumSamples_.size());
  analyzer_.update(spectrumSamples_.data(), window.count, window.sampleRate,
                   window.gain, dtMs);
  spectrum_.setLevels(analyzer_.levels());
#ifdef KNOBIFY_SPECTRUM_DEBUG
  static uint32_t frames = 0;
  static int64_t worstUs = 0;
  int64_t tookUs = esp_timer_get_time() - startUs;
  if (tookUs > worstUs) worstUs = tookUs;
  if (++frames % 90 == 0) {
    Serial.printf("[spectrum] n=%u rate=%u gain=%.3f worst=%lldus\n",
                  static_cast<unsigned>(window.count),
                  static_cast<unsigned>(window.sampleRate), window.gain,
                  worstUs);
    worstUs = 0;
  }
#endif
}

void ScreenManager::setProgressRingVisible(bool visible) {
  if (!progressArcHost_) return;
  if (visible) {
    lv_obj_clear_flag(progressArcHost_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(progressArcHost_, LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::onTimePillPressed(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->shuttle_.hold(millis());
}

void ScreenManager::onTimePillReleased(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->shuttle_.release(millis());
}

void ScreenManager::applyShuttleIndicator() {
  if (!shuttleArcHost_ || !shuttleMarker_) return;
  if (shownShuttleHeld_) {
    shuttleArc_.setValue(static_cast<int32_t>(shownShuttleStep_));
    lv_obj_clear_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);
    // A volume HUD still fading out from before the hold would hide the
    // progress ring; the knob isn't setting volume now, so end it.
    if (volumeHudVisible_) volumeHudHideAtMs_ = 0;
  } else {
    lv_obj_add_flag(shuttleArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(shuttleMarker_, LV_OBJ_FLAG_HIDDEN);
  }
}

void ScreenManager::updateVolumeDisplay(uint32_t nowMs) {
  if (!volumeArcHost_ || !volumeHudLabel_) return;
  volumeArc_.setValue(static_cast<int32_t>(playback_.volume()));
  char volText[8];
  snprintf(volText, sizeof(volText), "%d", playback_.volume());
  lv_label_set_text(volumeHudLabel_, volText);
  if (!volumeHudVisible_) {
    volumeHudVisible_ = true;
    // One edge ring at a time (ux-guidelines §6).
    setProgressRingVisible(false);
    lv_obj_clear_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(volumeHudPill_, LV_OBJ_FLAG_HIDDEN);
  }
  volumeHudHideAtMs_ = nowMs + kVolumeHudTimeoutMs;
  // Same spot as a message; the value being set wins.
  messages_.hide();
}

void ScreenManager::tickVolumeHud(uint32_t nowMs) {
  if (!volumeHudVisible_) return;
  if (nowMs < volumeHudHideAtMs_) return;
  volumeHudVisible_ = false;
  lv_obj_add_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(volumeHudPill_, LV_OBJ_FLAG_HIDDEN);
  setProgressRingVisible(durationSeconds_ != 0);
}

void ScreenManager::updateElapsedTimeDisplay() {
  if (!elapsedLabel_) return;
  uint32_t elapsedMs = playback_.elapsedMs(millis());
  uint32_t totalSeconds = elapsedMs / 1000;
  bool held = shuttle_.isHeld();
  int8_t step = shuttle_.step();
  bool shuttleChanged = held != shownShuttleHeld_ || step != shownShuttleStep_;
  if (shuttleChanged) {
    // Holding the pill alone does nothing visible to the song, so say what
    // the knob does now until it's turned (user feedback 2026-09-15: the
    // hold-and-turn wasn't obvious). Gone with the first detent or release.
    // While winding, the same spot names the speed -- bigger and easier to
    // read than inside the pill (user feedback 2026-09-15). ASCII "x": the
    // built-in font has no "×".
    if (held && step == 0) {
      messages_.show("Turn knob to rewind or fast forward", kNowPlayingMessageAnchor,
                     millis(), kShuttleHintMs);
    } else if (held) {
      char speed[32];
      snprintf(speed, sizeof(speed), "%s %ux", step > 0 ? "Fast forward" : "Rewind",
               1u << std::abs(step));
      messages_.show(speed, kNowPlayingMessageAnchor, millis(), kShuttleHintMs);
    } else if (shownShuttleHeld_) {
      messages_.hide();
    }
    shownShuttleHeld_ = held;
    shownShuttleStep_ = step;
    applyShuttleIndicator();
  }
  if (static_cast<int32_t>(totalSeconds) != lastShownSecond_ || shuttleChanged) {
    lastShownSecond_ = static_cast<int32_t>(totalSeconds);
    durationSeconds_ = playback_.durationSeconds();
    unsigned em = static_cast<unsigned>(totalSeconds / 60);
    unsigned es = static_cast<unsigned>(totalSeconds % 60);
    unsigned dm = static_cast<unsigned>(durationSeconds_ / 60);
    unsigned ds = static_cast<unsigned>(durationSeconds_ % 60);
    char text[40];
    // The shuttle speed is shown in the message area instead (see above),
    // so the pill keeps showing the time and total while winding.
    if (timePill_ && durationSeconds_ != 0) {
      snprintf(text, sizeof(text),
               KNOBIFY_ICON_FAST_REWIND " %u:%02u / %u:%02u " KNOBIFY_ICON_FAST_FORWARD,
               em, es, dm, ds);
    } else if (timePill_) {
      snprintf(text, sizeof(text),
               KNOBIFY_ICON_FAST_REWIND " %u:%02u " KNOBIFY_ICON_FAST_FORWARD, em, es);
    } else if (durationSeconds_ != 0) {
      snprintf(text, sizeof(text), "%u:%02u / %u:%02u", em, es, dm, ds);
    } else {
      snprintf(text, sizeof(text), "%u:%02u", em, es);
    }
    lv_label_set_text(elapsedLabel_, text);
    // Honest: no duration known -> no progress ring at all, rather than
    // a ring that never moves.
    if (!volumeHudVisible_) setProgressRingVisible(durationSeconds_ != 0);
  }
  if (durationSeconds_ != 0) {
    progressArc_.setValue(static_cast<float>(elapsedMs) /
                          (static_cast<float>(durationSeconds_) * 1000.0f));
  }
}

std::string ScreenManager::friendlyName(const std::string &path) {
  auto slash = path.find_last_of('/');
  std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
  auto dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    name = name.substr(0, dot);
  }
  return name;
}

void ScreenManager::onListMove(int16_t delta) {
  if (tiles_) {
    moveHomeSelection(static_cast<int>(delta));
    return;
  }
  if (!list_) return;
  int count = static_cast<int>(lv_obj_get_child_cnt(list_));
  if (count == 0) return;
  highlightedIndex_ =
      std::clamp(highlightedIndex_ + static_cast<int>(delta), 0, count - 1);
  applyHighlight();
  // Keep the knob-selected row on screen -- without this the highlight
  // moved past the list's visible area and vanished, since only touch
  // scrolling moved the view (found on real hardware 2026-09-13). Scrolls
  // just enough to reveal the row, so a selection already in view (e.g.
  // after touch-scrolling there) doesn't jump.
  lv_obj_t *selected = lv_obj_get_child(list_, highlightedIndex_);
  if (selected) lv_obj_scroll_to_view(selected, LV_ANIM_ON);
}

void ScreenManager::goToNowPlaying() {
  tabs_.activeStack().push(Screen{ScreenKind::NowPlaying, {}});
  render();
}

std::string ScreenManager::titleKeyFor(library::AlbumId albumId) const {
  const auto trackIds = library().tracksFor(albumId);
  if (trackIds.empty()) return "";
  return resume::BookmarkKeeper::titleKeyFor(
      library().tracks[trackIds[0]].filePath);
}

bool ScreenManager::hasContinueRow(const navigation::Screen &screen,
                                   resume::Bookmark &out) const {
  if (screen.kind != ScreenKind::Tracks) return false;
  if (!profile().resumesWithinTitle) return false;
  const std::string key = titleKeyFor(screen.params.albumId);
  if (key.empty()) return false;
  if (!bookmarks_.lookup(key, out)) return false;
  // A bookmark whose part is no longer in the album (renamed, deleted,
  // rescanned away) would resume nothing -- don't offer it.
  for (library::TrackId id : library().tracksFor(screen.params.albumId)) {
    if (library().tracks[id].filePath == out.trackPath) return true;
  }
  return false;
}

// Cue at the saved position, then start -- the same two steps the session
// resume takes when the user presses play, so the seek behaviour is
// identical rather than a second way of doing it.
void ScreenManager::playFromBookmark(const resume::Bookmark &mark,
                                     library::AlbumId albumId) {
  std::vector<std::string> playlist =
      library::PlaylistBuilder::forAlbum(library(), albumId);
  size_t index = 0;
  bool found = false;
  for (size_t i = 0; i < playlist.size(); ++i) {
    if (playlist[i] != mark.trackPath) continue;
    index = i;
    found = true;
    break;
  }
  if (!found) return;
  playback_.cue(std::move(playlist), index, /*shuffle=*/false,
                playback::PlayScope::Album, mark.filePosition,
                mark.elapsedSeconds, millis());
  playback_.togglePlayPause(millis());
  goToNowPlaying();
}

void ScreenManager::onListItemClicked(lv_event_t *e) {
  auto *ctx = static_cast<ItemContext *>(lv_event_get_user_data(e));
  ScreenManager *self = ctx->self;
  Screen current = self->tabs_.activeStack().current();

  if (ctx->isContinue) {
    resume::Bookmark mark;
    if (self->hasContinueRow(current, mark)) {
      self->playFromBookmark(mark, current.params.albumId);
    }
    return;
  }

  if (ctx->isShuffle) {
    std::vector<std::string> playlist;
    std::string name = "library";
    playback::PlayScope scope;
    if (current.kind == ScreenKind::Artists) {
      playlist = library::PlaylistBuilder::forLibrary(self->library(), self->profile().sort);
      scope = playback::PlayScope::Library;
    } else if (current.kind == ScreenKind::Albums) {
      playlist = library::PlaylistBuilder::forArtist(self->library(), self->profile().sort,
                                                     current.params.artistId);
      scope = playback::PlayScope::Artist;
      name = self->library().artists[current.params.artistId].name;
    } else {
      playlist = library::PlaylistBuilder::forAlbum(self->library(),
                                                    current.params.albumId);
      scope = playback::PlayScope::Album;
      name = self->library().albums[current.params.albumId].title;
    }
    if (playlist.empty()) return;
    self->playback_.play(std::move(playlist), 0, millis(), /*shuffle=*/true,
                         scope);
    self->goToNowPlaying();
    self->messages_.show(("Shuffling " + name).c_str(), kNowPlayingMessageAnchor,
                         millis());
    return;
  }

  switch (current.kind) {
    case ScreenKind::Artists:
      self->tabs_.activeStack().push(
          Screen{ScreenKind::Albums,
                 ScreenParams{.artistId = static_cast<uint32_t>(
                                  self->library().artistsSorted(
                                      self->profile().sort)[ctx->index]),
                              .collection = current.params.collection}});
      self->render();
      break;
    case ScreenKind::Albums:
      self->tabs_.activeStack().push(Screen{
          ScreenKind::Tracks,
          ScreenParams{.albumId = static_cast<uint32_t>(
                           self->library().albumsFor(
                               current.params.artistId,
                               self->profile().sort)[ctx->index]),
                       .collection = current.params.collection}});
      self->render();
      break;
    case ScreenKind::Tracks: {
      // Rows follow tracksFor() order, so the row index is the start index.
      // In order, shuffle off: a tapped track always plays its album as listed.
      self->playback_.play(
          library::PlaylistBuilder::forAlbum(self->library(),
                                             current.params.albumId),
          static_cast<size_t>(ctx->index), millis(), /*shuffle=*/false,
          playback::PlayScope::Album);
      self->goToNowPlaying();
      break;
    }
    case ScreenKind::Folder:
      if (ctx->isFolder) {
        self->tabs_.activeStack().push(
            Screen{ScreenKind::Folder,
                   ScreenParams{.folderPath = ctx->path,
                                .collection = current.params.collection}});
        self->render();
      } else {
        self->playback_.play({ctx->path}, 0, millis());
        self->goToNowPlaying();
      }
      break;
    case ScreenKind::Settings:
      if (ctx->index >= 0 && ctx->index < kSettingsRowCount) {
        kSettingsRows[ctx->index].open(*self);
      }
      break;
    case ScreenKind::RescanPicker:
      if (ctx->index >= 0 &&
          collection::isValidCollection(static_cast<uint8_t>(ctx->index))) {
        self->runRescan(static_cast<collection::CollectionId>(ctx->index));
      } else {
        self->runRescanAll();
      }
      break;
    case ScreenKind::MenuVisibility:
      self->toggleMenuEntryVisible(ctx->index);
      break;
    default:
      break;
  }
}

void ScreenManager::onBackClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->tabs_.back();
  self->render();
}

void ScreenManager::onMiniBarClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->goToNowPlaying();
}

void ScreenManager::onPrevClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.prev(millis());
  self->render();
}

void ScreenManager::onPlayPauseClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.togglePlayPause(millis());
  self->render();
}

void ScreenManager::onNextClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.next(millis());
  self->render();
}

void ScreenManager::onCoverSwitchClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->preferSpectrum_ = !self->preferSpectrum_;
  self->settings_.setU8(kSpectrumSettingKey, self->preferSpectrum_ ? 1 : 0);
  // Re-render rather than applyCoverSlotMode(): the panel's switch shows
  // what it switches to next, so it changes too.
  self->render();
}

void ScreenManager::onOptionsHandleClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->optionsPanelOpen_ = true;
  self->animateOptionsPanel_ = true;
  self->render();
}

void ScreenManager::onOptionsPanelCloseClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  // The scrim gets bubbled clicks from nothing (children don't bubble by
  // default), so a click here is on the scrim itself or the close chevron.
  self->optionsPanelOpen_ = false;
  self->render();
}

void ScreenManager::onShuffleClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.setShuffle(!self->playback_.shuffle());
  self->render();
  self->showShuffleMessage();
}

void ScreenManager::onRepeatClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.cycleRepeat();
  self->settings_.setU8(kRepeatSettingKey,
                        static_cast<uint8_t>(self->playback_.repeat()));
  self->render();
  self->showRepeatMessage();
}

// The icon alone didn't say which mode was active (user feedback
// 2026-09-15) -- the message says what the toggle now does, naming the scope.
void ScreenManager::showShuffleMessage() {
  const char *text = "Shuffle off - in order";
  if (playback_.shuffle()) {
    switch (playback_.scope()) {
      case playback::PlayScope::Album:
        text = "Shuffle on - album";
        break;
      case playback::PlayScope::Artist:
        text = "Shuffle on - artist";
        break;
      case playback::PlayScope::Library:
        text = "Shuffle on - library";
        break;
      case playback::PlayScope::File:
        text = "Shuffle on";
        break;
    }
  }
  messages_.show(text, kNowPlayingMessageAnchor, millis());
}

void ScreenManager::showRepeatMessage() {
  const char *text = "Repeat off";
  if (playback_.repeat() == playback::RepeatMode::All) text = "Repeat all";
  if (playback_.repeat() == playback::RepeatMode::One) text = "Repeat this track";
  messages_.show(text, kNowPlayingMessageAnchor, millis());
}

void ScreenManager::onLockClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  // Lock lives in the options panel (ADR 0014); close it so Now Playing is
  // back to normal after unlocking.
  self->optionsPanelOpen_ = false;
  self->render();
  self->lockController_.requestLock();
  // LockOverlay (shown on LVGL's top layer, independent of ScreenManager)
  // picks up the new lock state on its own next tick() and covers this
  // screen.
}

// Runs from a tap on a Settings > Rescan row -- the only way to pick up
// new/changed files, since boot only loads the cached indexes.
void ScreenManager::runRescan(collection::CollectionId id) {
  // A one-shot full-screen overlay on LVGL's top layer, same technique as
  // LockOverlay -- but built and torn down here rather than a persistent
  // begin()/tick() class, since a rescan is a single blocking call, not
  // ongoing state to poll every loop(). rescan() itself blocks (it's the
  // same synchronous SD walk/scan the old boot path used), so the device
  // is unresponsive for its duration -- acceptable here since the user
  // just explicitly asked for this, unlike the old always-blocking boot.
  lv_obj_t *overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(overlay, theme::surface(), 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(overlay);
  char scanning[64];
  snprintf(scanning, sizeof(scanning), "Scanning for changes\nin %s...",
           collections_.profile(id).label);
  lv_label_set_text(label, scanning);
  lv_obj_set_style_text_color(label, theme::ink(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  // lv_refr_now(), not lv_timer_handler() -- see ScanProgressLabelListener's
  // comment above for why a reentrant lv_timer_handler() call from here
  // corrupts input state.
  lv_refr_now(nullptr);

  ScanProgressLabelListener progress(label);
  collections_.rescan(id, &progress);

  // Async, not lv_obj_del() -- we're still inside the click event that
  // LVGL's own (outer) lv_timer_handler() is currently dispatching;
  // deleting synchronously here risks the same input-state corruption
  // ScreenManager::render() already guards against for screen_ below.
  lv_obj_del_async(overlay);
  render();
}

void ScreenManager::runRescanAll() {
  // Each one puts up (and tears down) its own overlay, so the label names
  // whichever collection is actually being walked rather than a single
  // "Scanning..." that sits there for minutes saying nothing.
  for (const auto &collectionProfile : collection::kCollections) {
    runRescan(collectionProfile.id);
  }
}

}  // namespace knobify::ui
