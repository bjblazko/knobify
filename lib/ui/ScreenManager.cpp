#include "ScreenManager.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>

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

void ScreenManager::begin() {
  uint8_t stored = 0;
  preferSpectrum_ = settings_.getU8(kSpectrumSettingKey, stored) && stored != 0;
  if (settings_.getU8(kRepeatSettingKey, stored) && stored <= 2) {
    playback_.setRepeat(static_cast<playback::RepeatMode>(stored));
  }
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
  miniBar_ = nullptr;
  elapsedLabel_ = nullptr;
  coverImg_ = nullptr;
  spectrum_.detach();
  volumeArcHost_ = nullptr;
  volumeHudPill_ = nullptr;
  volumeHudLabel_ = nullptr;
  volumeHudVisible_ = false;
  progressArcHost_ = nullptr;
  lastShownSecond_ = -1;
  durationSeconds_ = 0;

  Screen current = tabs_.activeStack().current();

  if (current.kind == ScreenKind::NowPlaying) {
    renderNowPlaying();
  } else if (current.kind == ScreenKind::Home) {
    renderHome();
  } else if (current.kind == ScreenKind::Brightness) {
    renderBrightness();
  } else {
    std::vector<std::pair<std::string, int>> items;
    // Library lists start with a Shuffle row whose scope is the list itself:
    // the whole library, this artist, this album (ADR 0011). No scope
    // setting -- where you start is the scope.
    if (hasShuffleRow(current.kind)) {
      items.emplace_back(LV_SYMBOL_SHUFFLE "  Shuffle", kShuffleItemId);
    }
    switch (current.kind) {
      case ScreenKind::Artists:
        for (const auto &artist : library_.artists) {
          items.emplace_back(artist.name, static_cast<int>(artist.id));
        }
        break;
      case ScreenKind::Albums:
        for (auto albumId : library_.albumsFor(current.params.artistId)) {
          for (const auto &album : library_.albums) {
            if (album.id == albumId) items.emplace_back(album.title, albumId);
          }
        }
        break;
      case ScreenKind::Tracks:
        for (auto trackId : library_.tracksFor(current.params.albumId)) {
          for (const auto &track : library_.tracks) {
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
        items.emplace_back("Brightness", 0);
        items.emplace_back("Rescan library", 1);
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
    // year is unknown. No explicit color: it inherits the row's text color
    // at reduced opacity, so it stays readable on the ink selected row too.
    if (current.kind == ScreenKind::Albums) {
      for (const auto &album : library_.albums) {
        if (album.id != static_cast<library::AlbumId>(items[i].second) ||
            album.year == 0) {
          continue;
        }
        char yearText[8];
        snprintf(yearText, sizeof(yearText), "%u",
                 static_cast<unsigned>(album.year));
        lv_obj_t *yearLabel = lv_label_create(btn);
        lv_obj_set_style_text_font(yearLabel, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_opa(yearLabel, LV_OPA_60, 0);
        lv_label_set_text(yearLabel, yearText);
        lv_obj_set_style_pad_column(btn, 10, 0);
        break;
      }
    }

    // The Brightness row ends in its current value, styled like the album
    // year: a plain secondary fact, not a badge.
    if (current.kind == ScreenKind::Settings && items[i].second == 0) {
      char valueText[8];
      snprintf(valueText, sizeof(valueText), "%u%%",
               static_cast<unsigned>(brightness_.percent()));
      lv_obj_t *valueLabel = lv_label_create(btn);
      lv_obj_set_style_text_font(valueLabel, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_opa(valueLabel, LV_OPA_60, 0);
      lv_label_set_text(valueLabel, valueText);
      lv_obj_set_style_pad_column(btn, 10, 0);
    }

    // After the year label exists: the title label flex-grows into
    // whatever width the year leaves, and truncates within that.
    if (label) setClampedText(label, items[i].first.c_str(), 1);

    auto ctx = std::make_unique<ItemContext>();
    ctx->self = this;
    ctx->isShuffle = hasShuffleRow(current.kind) && i == 0;
    // Index into the screen's data (artists, albums, tracks), not the row.
    ctx->index = static_cast<int>(i) - (hasShuffleRow(current.kind) ? 1 : 0);
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
      caption = "Library";
      break;
    case ScreenKind::Settings:
      caption = "Settings";
      break;
    case ScreenKind::Brightness:
      caption = "Brightness";
      break;
    case ScreenKind::Albums:
      for (const auto &artist : library_.artists) {
        if (artist.id == current.params.artistId) caption = artist.name;
      }
      break;
    case ScreenKind::Tracks:
      for (const auto &album : library_.albums) {
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
  for (const auto &track : library_.tracks) {
    if (track.filePath != path) continue;
    info.title = track.title;
    for (const auto &album : library_.albums) {
      if (album.id != track.albumId) continue;
      info.album = album.title;
      for (const auto &artist : library_.artists) {
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
  if (coverImg_) {
    // Only switchable when there's something to switch to.
    for (lv_obj_t *slot : {coverImg_, spectrum}) {
      lv_obj_add_flag(slot, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(slot, &ScreenManager::onCoverSlotClicked,
                          LV_EVENT_CLICKED, this);
    }
  }
  applyCoverSlotMode();
  lastSpectrumTickMs_ = millis();

  const lv_coord_t slotSize = ui_widgets::DotMatrixSpectrum::kSize;
  lv_coord_t titleY = kCoverY + slotSize + 12;
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

  // Shuffle and repeat toggles beside the time readout (ADR 0011): quiet
  // buttons whose glyph turns confirm green while active -- state, not a
  // second call to action, so Play/Pause stays the only accent. Created
  // before the transport row so prev/next win where hit areas meet, and
  // with no extended hit area: prev/next's own slop reaches down to them.
  // x/y keep the 28px glyph inside the bezel at this height.
  constexpr lv_coord_t kToggleX = 100;
  const lv_coord_t toggleY = kTransportCenterY + 56 - 22;
  auto makeToggle = [&](const char *glyph, bool active, lv_coord_t x,
                        lv_event_cb_t cb) {
    lv_obj_t *btn = makeIconButton(screen_, glyph, 44, 44, LV_ALIGN_TOP_MID, x,
                                   toggleY, cb, this, ButtonRole::Quiet,
                                   &knobify_icon_font_28);
    lv_obj_set_ext_click_area(btn, 0);
    if (active) {
      lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), theme::confirm(), 0);
    }
  };
  makeToggle(KNOBIFY_ICON_SHUFFLE, playback_.shuffle(), -kToggleX,
             &ScreenManager::onShuffleClicked);
  playback::RepeatMode repeat = playback_.repeat();
  makeToggle(repeat == playback::RepeatMode::One ? KNOBIFY_ICON_REPEAT_ONE
                                                 : KNOBIFY_ICON_REPEAT,
             repeat != playback::RepeatMode::Off, kToggleX,
             &ScreenManager::onRepeatClicked);

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
  elapsedLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(elapsedLabel_, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(elapsedLabel_, theme::structure(), 0);
  lv_obj_align(elapsedLabel_, LV_ALIGN_TOP_MID, 0, kTransportCenterY + 44);
  lv_label_set_text(elapsedLabel_, "0:00");
  updateElapsedTimeDisplay();

  // Lock button -- bottom-center, inset from the edge like the back
  // button's top-center inset, clear of the round bezel and of the
  // controls above.
  makeIconButton(screen_, KNOBIFY_ICON_LOCK, kHeaderButtonW, kHeaderButtonH,
                 LV_ALIGN_BOTTOM_MID, 0, -kHeaderButtonY,
                 &ScreenManager::onLockClicked, this, ButtonRole::Quiet,
                 &knobify_icon_font_28);
}

void ScreenManager::applyHighlight() {
  if (tiles_) {
    // Home: the selected tile's circle (each cell's first child) turns ink.
    uint32_t count = lv_obj_get_child_cnt(tiles_);
    for (uint32_t i = 0; i < count; ++i) {
      lv_obj_t *circle = lv_obj_get_child(lv_obj_get_child(tiles_, i), 0);
      if (static_cast<int>(i) == highlightedIndex_) {
        lv_obj_add_state(circle, LV_STATE_CHECKED);
      } else {
        lv_obj_clear_state(circle, LV_STATE_CHECKED);
      }
    }
    return;
  }
  if (!list_) return;
  // Toggling LV_STATE_CHECKED and letting the theme render it (rather
  // than overriding bg_color by hand) guarantees the theme's own
  // contrast-correct text/background pairing for both states.
  uint32_t count = lv_obj_get_child_cnt(list_);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t *btn = lv_obj_get_child(list_, i);
    if (static_cast<int>(i) == highlightedIndex_) {
      lv_obj_add_state(btn, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(btn, LV_STATE_CHECKED);
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
  if (static_cast<int32_t>(totalSeconds) != lastShownSecond_) {
    lastShownSecond_ = static_cast<int32_t>(totalSeconds);
    durationSeconds_ = playback_.durationSeconds();
    char text[24];
    if (durationSeconds_ != 0) {
      snprintf(text, sizeof(text), "%u:%02u / %u:%02u",
               static_cast<unsigned>(totalSeconds / 60),
               static_cast<unsigned>(totalSeconds % 60),
               static_cast<unsigned>(durationSeconds_ / 60),
               static_cast<unsigned>(durationSeconds_ % 60));
    } else {
      snprintf(text, sizeof(text), "%u:%02u",
               static_cast<unsigned>(totalSeconds / 60),
               static_cast<unsigned>(totalSeconds % 60));
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
    int count = static_cast<int>(lv_obj_get_child_cnt(tiles_));
    if (count == 0) return;
    highlightedIndex_ =
        std::clamp(highlightedIndex_ + static_cast<int>(delta), 0, count - 1);
    homeSelection_ = highlightedIndex_;
    applyHighlight();
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

void ScreenManager::onListItemClicked(lv_event_t *e) {
  auto *ctx = static_cast<ItemContext *>(lv_event_get_user_data(e));
  ScreenManager *self = ctx->self;
  Screen current = self->tabs_.activeStack().current();

  if (ctx->isShuffle) {
    std::vector<std::string> playlist;
    if (current.kind == ScreenKind::Artists) {
      playlist = library::PlaylistBuilder::forLibrary(self->library_);
    } else if (current.kind == ScreenKind::Albums) {
      playlist = library::PlaylistBuilder::forArtist(self->library_,
                                                     current.params.artistId);
    } else {
      playlist = library::PlaylistBuilder::forAlbum(self->library_,
                                                    current.params.albumId);
    }
    if (playlist.empty()) return;
    self->playback_.play(std::move(playlist), 0, millis(), /*shuffle=*/true);
    self->goToNowPlaying();
    return;
  }

  switch (current.kind) {
    case ScreenKind::Artists:
      self->tabs_.activeStack().push(
          Screen{ScreenKind::Albums, ScreenParams{.artistId = static_cast<uint32_t>(
                                          self->library_.artists[ctx->index].id)}});
      self->render();
      break;
    case ScreenKind::Albums:
      self->tabs_.activeStack().push(Screen{
          ScreenKind::Tracks,
          ScreenParams{.albumId = static_cast<uint32_t>(
                           self->library_.albumsFor(current.params.artistId)[ctx->index])}});
      self->render();
      break;
    case ScreenKind::Tracks: {
      // Rows follow tracksFor() order, so the row index is the start index.
      // In order, shuffle off: a tapped track always plays its album as listed.
      self->playback_.play(
          library::PlaylistBuilder::forAlbum(self->library_,
                                             current.params.albumId),
          static_cast<size_t>(ctx->index), millis());
      self->goToNowPlaying();
      break;
    }
    case ScreenKind::Folder:
      if (ctx->isFolder) {
        self->tabs_.activeStack().push(
            Screen{ScreenKind::Folder, ScreenParams{.folderPath = ctx->path}});
        self->render();
      } else {
        self->playback_.play({ctx->path}, 0, millis());
        self->goToNowPlaying();
      }
      break;
    case ScreenKind::Settings:
      if (ctx->index == 0) {
        self->tabs_.activeStack().push(Screen{ScreenKind::Brightness, {}});
        self->render();
      } else {
        self->runRescan();
      }
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

void ScreenManager::onCoverSlotClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->preferSpectrum_ = !self->preferSpectrum_;
  self->applyCoverSlotMode();
  self->settings_.setU8(kSpectrumSettingKey, self->preferSpectrum_ ? 1 : 0);
}

void ScreenManager::onShuffleClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.setShuffle(!self->playback_.shuffle());
  self->render();
}

void ScreenManager::onRepeatClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.cycleRepeat();
  self->settings_.setU8(kRepeatSettingKey,
                        static_cast<uint8_t>(self->playback_.repeat()));
  self->render();
}

void ScreenManager::onLockClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->lockController_.requestLock();
  // No re-render needed here: LockOverlay (shown on LVGL's top layer,
  // independent of ScreenManager) picks up the new lock state on its own
  // next tick() and covers this screen.
}

// Runs from a tap on Settings' "Rescan library" row -- the only way to pick
// up new/changed music, since boot only loads the cached index.
void ScreenManager::runRescan() {
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
  lv_label_set_text(label, "Scanning for changes\nin music library...");
  lv_obj_set_style_text_color(label, theme::ink(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  // lv_refr_now(), not lv_timer_handler() -- see ScanProgressLabelListener's
  // comment above for why a reentrant lv_timer_handler() call from here
  // corrupts input state.
  lv_refr_now(nullptr);

  ScanProgressLabelListener progress(label);
  rescanner_.rescan(&progress);

  // Async, not lv_obj_del() -- we're still inside the click event that
  // LVGL's own (outer) lv_timer_handler() is currently dispatching;
  // deleting synchronously here risks the same input-state corruption
  // ScreenManager::render() already guards against for screen_ below.
  lv_obj_del_async(overlay);
  render();
}

}  // namespace knobify::ui
