#include "ScreenManager.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>

#include "IconFont.h"
#include "LvglButtonHelpers.h"
#include "St77916Driver.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::ScreenParams;

namespace knobify::ui {

namespace {

// Updates the scan overlay's label as LibraryRescanner::rescan() walks the
// SD card -- same text/throttling as the old boot-time listener this
// replaced (main.cpp no longer scans at boot, see AGENTS.md). Uses
// lv_refr_now(), NOT lv_timer_handler(), to flush the label: rescan() runs
// synchronously from inside onScanClicked, which is itself invoked BY an
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

void ScreenManager::begin() { render(); }

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
  miniBar_ = nullptr;
  elapsedLabel_ = nullptr;
  volumeArcHost_ = nullptr;
  volumeHudLabel_ = nullptr;
  volumeHudVisible_ = false;

  Screen current = tabs_.activeStack().current();

  if (current.kind == ScreenKind::NowPlaying) {
    renderNowPlaying();
  } else {
    std::vector<std::pair<std::string, int>> items;
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
  renderScanButtonIfNeeded();
}

void ScreenManager::renderList(
    const std::vector<std::pair<std::string, int>> &items, bool showMiniBar) {
  Screen current = tabs_.activeStack().current();
  list_ = lv_list_create(screen_);
  lv_obj_set_size(list_, drivers::kLcdHorRes,
                   showMiniBar ? drivers::kLcdVerRes - 40 : drivers::kLcdVerRes);
  lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 0);
  // The default scrollbar sits flush against the right edge, which on a
  // round display gets clipped by the bezel there (looked like a
  // stray pink line, cut off mid-stroke) -- the highlighted item already
  // shows position for knob-driven scrolling, so the scrollbar itself
  // isn't needed. Side padding keeps row text off the round-unsafe
  // edges too.
  lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
  // Asymmetric: text reads left-to-right from its start, so pushing the
  // left edge in further than the right keeps the start of each row's
  // text clear of the round bezel's curve without wasting space on the
  // (less legibility-critical) trailing/ellipsis end.
  lv_obj_set_style_pad_left(list_, 44, 0);
  lv_obj_set_style_pad_right(list_, 24, 0);
  // The round bezel clips much more aggressively near the very top than
  // the sides do -- without this, the first row's text got cut off
  // (found on real hardware 2026-09-12). Also clears space for the back
  // button (see renderBackButtonIfNeeded()) on non-root screens.
  lv_obj_set_style_pad_top(list_, 40, 0);

  std::vector<library::FolderEntry> folderEntries;
  if (current.kind == ScreenKind::Folder) {
    folderEntries =
        library::FolderBrowser::list(directoryReader_, current.params.folderPath);
  }

  for (size_t i = 0; i < items.size(); ++i) {
    lv_obj_t *btn = lv_list_add_btn(list_, nullptr, items[i].first.c_str());
    // Checkable + the theme's own checked style (rather than a manual
    // bg_color override) keeps text contrast correct for free -- see
    // applyHighlight().
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_pad_top(btn, 10, 0);
    lv_obj_set_style_pad_bottom(btn, 10, 0);

    // lv_list_add_btn's label defaults to a scrolling marquee for long
    // text and the small default font -- a bigger font plus a clean
    // ellipsis truncation reads better on a small round display than
    // several artist/album names all mid-scroll at once.
    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label) {
      lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }

    auto ctx = std::make_unique<ItemContext>();
    ctx->self = this;
    ctx->index = static_cast<int>(i);
    ctx->albumId = current.params.albumId;
    if (current.kind == ScreenKind::Tracks) {
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

  if (showMiniBar) {
    // Narrower than full width and inset from the very bottom edge --
    // flush-bottom, full-width was clipped by the round bezel down to a
    // sliver, which read as "too small, stuck in a corner" (found on
    // real hardware 2026-09-12). This width/inset keeps it within the
    // round-safe area at this height.
    miniBar_ = lv_obj_create(screen_);
    lv_obj_set_size(miniBar_, 260, 48);
    lv_obj_align(miniBar_, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_t *label = lv_label_create(miniBar_);
    lv_label_set_text(label, friendlyName(playback_.currentPath()).c_str());
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 220);
    lv_obj_center(label);
    lv_obj_add_flag(miniBar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(miniBar_, &ScreenManager::onMiniBarClicked,
                         LV_EVENT_CLICKED, this);
  }

  applyHighlight();
}

void ScreenManager::renderBackButtonIfNeeded() {
  // Swiping left-to-right also goes back (decision 5, ADR 0004), but
  // real usage showed that's not discoverable on its own -- a user
  // reaching Now Playing had no visible way back at all. This is a
  // supplementary, always-visible affordance for the same action.
  // Positioned top-center rather than a corner: the round bezel clips
  // corners much more aggressively than top-center at this height.
  if (!tabs_.activeStack().canGoBack()) return;
  makeIconButton(screen_, LV_SYMBOL_LEFT, 56, 32, LV_ALIGN_TOP_MID, 0, 12,
                 &ScreenManager::onBackClicked, this);
}

void ScreenManager::renderScanButtonIfNeeded() {
  // Boot no longer scans the SD card at all (just loads whatever library
  // index was last cached, see AGENTS.md) -- this is the only way to pick
  // up new/changed music. Artists is always the Library tab's root today
  // (see ScreenId.h), so this doubles as "top of the library list" without
  // needing a separate marker. Same top-center slot as the back button;
  // mutually exclusive with it since a stack root never canGoBack().
  if (tabs_.activeStack().current().kind != ScreenKind::Artists) return;
  makeIconButton(screen_, LV_SYMBOL_REFRESH, 56, 32, LV_ALIGN_TOP_MID, 0, 12,
                 &ScreenManager::onScanClicked, this);
}

void ScreenManager::renderNowPlaying() {
  lv_obj_t *label = lv_label_create(screen_);
  lv_label_set_text(label, friendlyName(playback_.currentPath()).c_str());
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_width(label, drivers::kLcdHorRes - 100);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

  // Album cover, centered between the song name and the transport
  // buttons -- the only round-safe gap left on this screen (see the
  // volume HUD/button comments below for the round-display placement
  // rules this file keeps re-deriving). Created before the volume HUD
  // widgets so LVGL's creation-order z-stacking lets the transient HUD
  // draw over the cover rather than under it. No cover cached for this
  // album -> no widget at all, matching this screen's existing
  // minimalism rather than showing a placeholder.
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
    // Vertical offset placeholder -- like every other offset on this
    // screen, needs on-device tuning within the round-safe gap between
    // the song label and the transport buttons (see ADR 0004).
    lv_obj_align(coverImg_, LV_ALIGN_CENTER, 0, -25);
  } else {
    coverImg_ = nullptr;
    coverPixels_.clear();
  }

  // Round-edge volume HUD: a ring flush against the physical bezel
  // (host sized to the full framebuffer, not inset -- the round bezel
  // itself clips the outer edge, which is what makes it read as flush
  // rather than leaving a visible gap, see ADR 0004's corner-clipping
  // notes) plus a numeric readout. Hidden until the first adjustment;
  // updateVolumeDisplay()/tickVolumeHud() show it and auto-hide it after
  // kVolumeHudTimeoutMs, like a phone's volume overlay -- replaces the
  // always-on linear bar (removed per user feedback 2026-09-13: it never
  // updated live, and permanently occupied center screen for a value
  // that's rarely being actively watched).
  ui_widgets::EdgeArcConfig volumeArcConfig;
  volumeArcConfig.startAngle = 135;
  volumeArcConfig.endAngle = 45;
  volumeArcConfig.widthPx = 12;
  // Indigo accent for the current level, grey for the unfilled track --
  // deliberately a different accent color from LockOverlay's unlock ring
  // (green) so the two aren't visually confusable as the same indicator
  // (found on real hardware 2026-09-13: both rendered identically blue
  // and were mistaken for each other).
  volumeArcConfig.color = lv_palette_main(LV_PALETTE_INDIGO);
  volumeArcConfig.hasBackgroundColor = true;
  volumeArcConfig.backgroundColor = lv_palette_lighten(LV_PALETTE_GREY, 2);
  // Oversized beyond the screen's own bounds and let the screen object's
  // default clipping (plus the physical round bezel) eat the excess --
  // even with EdgeArc's knob-padding fix, sizing the host to exactly
  // kLcdHorRes/VerRes still left a visible gap from the true edge on
  // real hardware (some further LVGL-internal margin), and overshooting
  // is harmless here since nothing else occupies that space.
  volumeArcHost_ = lv_obj_create(screen_);
  lv_obj_set_size(volumeArcHost_, drivers::kLcdHorRes + 40,
                   drivers::kLcdVerRes + 40);
  lv_obj_center(volumeArcHost_);
  lv_obj_set_style_bg_opa(volumeArcHost_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(volumeArcHost_, 0, 0);
  lv_obj_clear_flag(volumeArcHost_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(volumeArcHost_, LV_OBJ_FLAG_CLICKABLE);
  volumeArc_.create(volumeArcHost_, volumeArcConfig,
                     playback::PlaybackStateMachine::kMinVolume,
                     playback::PlaybackStateMachine::kMaxVolume);
  volumeArc_.setValue(static_cast<int32_t>(playback_.volume()));
  lv_obj_add_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);

  volumeHudLabel_ = lv_label_create(screen_);
  lv_obj_set_style_text_font(volumeHudLabel_, &lv_font_montserrat_20, 0);
  lv_obj_align(volumeHudLabel_, LV_ALIGN_CENTER, 0, -34);
  char volText[8];
  snprintf(volText, sizeof(volText), "%d", playback_.volume());
  lv_label_set_text(volumeHudLabel_, volText);
  lv_obj_add_flag(volumeHudLabel_, LV_OBJ_FLAG_HIDDEN);

  // Buttons sit inset from the edges, safely within the round display's
  // visible area rather than at the literal corners -- see decision 6,
  // ADR 0004.
  makeIconButton(screen_, LV_SYMBOL_PREV, 70, 70, LV_ALIGN_CENTER, -90, 60,
                 &ScreenManager::onPrevClicked, this);

  makeIconButton(screen_,
                 playback_.state() == playback::PlaybackState::Playing
                     ? LV_SYMBOL_PAUSE
                     : LV_SYMBOL_PLAY,
                 80, 80, LV_ALIGN_CENTER, 0, 60,
                 &ScreenManager::onPlayPauseClicked, this);

  makeIconButton(screen_, LV_SYMBOL_NEXT, 70, 70, LV_ALIGN_CENTER, 90, 60,
                 &ScreenManager::onNextClicked, this);

  // Elapsed play time -- requested after real hardware testing made it
  // clear there was no way to tell whether playback was actually
  // progressing. Wall-clock time since the track started minus paused
  // time (see PlaybackStateMachine::elapsedMs()), not the decoder's own
  // position -- close enough for a simple readout.
  elapsedLabel_ = lv_label_create(screen_);
  lv_obj_align(elapsedLabel_, LV_ALIGN_CENTER, 0, 110);
  lv_label_set_text(elapsedLabel_, "0:00");
  updateElapsedTimeDisplay();

  // Lock button -- placement is preliminary (this screen's layout has
  // repeatedly needed hardware-driven adjustment, per ADR 0004) but
  // bottom-center, inset from the edge like the back button's top-center
  // inset, keeps it clear of the round bezel and of the controls above.
  makeIconButton(screen_, KNOBIFY_ICON_LOCK, 56, 32, LV_ALIGN_BOTTOM_MID, 0,
                 -12, &ScreenManager::onLockClicked, this,
                 &knobify_icon_font_28);
}

void ScreenManager::applyHighlight() {
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

void ScreenManager::updateVolumeDisplay(uint32_t nowMs) {
  if (!volumeArcHost_ || !volumeHudLabel_) return;
  volumeArc_.setValue(static_cast<int32_t>(playback_.volume()));
  char volText[8];
  snprintf(volText, sizeof(volText), "%d", playback_.volume());
  lv_label_set_text(volumeHudLabel_, volText);
  if (!volumeHudVisible_) {
    volumeHudVisible_ = true;
    lv_obj_clear_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(volumeHudLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  volumeHudHideAtMs_ = nowMs + kVolumeHudTimeoutMs;
}

void ScreenManager::tickVolumeHud(uint32_t nowMs) {
  if (!volumeHudVisible_) return;
  if (nowMs < volumeHudHideAtMs_) return;
  volumeHudVisible_ = false;
  lv_obj_add_flag(volumeArcHost_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(volumeHudLabel_, LV_OBJ_FLAG_HIDDEN);
}

void ScreenManager::updateElapsedTimeDisplay() {
  if (!elapsedLabel_) return;
  uint32_t totalSeconds = playback_.elapsedMs(millis()) / 1000;
  uint32_t minutes = totalSeconds / 60;
  uint32_t seconds = totalSeconds % 60;
  char text[16];
  snprintf(text, sizeof(text), "%u:%02u", static_cast<unsigned>(minutes),
           static_cast<unsigned>(seconds));
  lv_label_set_text(elapsedLabel_, text);
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
  if (!list_) return;
  int count = static_cast<int>(lv_obj_get_child_cnt(list_));
  if (count == 0) return;
  highlightedIndex_ =
      std::clamp(highlightedIndex_ + static_cast<int>(delta), 0, count - 1);
  applyHighlight();
}

void ScreenManager::goToNowPlaying() {
  tabs_.activeStack().push(Screen{ScreenKind::NowPlaying, {}});
  render();
}

void ScreenManager::onListItemClicked(lv_event_t *e) {
  auto *ctx = static_cast<ItemContext *>(lv_event_get_user_data(e));
  ScreenManager *self = ctx->self;
  Screen current = self->tabs_.activeStack().current();

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
      auto trackIds = self->library_.tracksFor(current.params.albumId);
      std::vector<std::string> playlist;
      size_t startIndex = 0;
      for (size_t i = 0; i < trackIds.size(); ++i) {
        for (const auto &track : self->library_.tracks) {
          if (track.id == trackIds[i]) {
            if (track.id == ctx->trackId) startIndex = playlist.size();
            playlist.push_back(track.filePath);
          }
        }
      }
      self->playback_.play(playlist, startIndex, millis());
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
    default:
      break;
  }
}

void ScreenManager::onBackClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->tabs_.activeStack().pop();
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

void ScreenManager::onLockClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->lockController_.requestLock();
  // No re-render needed here: LockOverlay (shown on LVGL's top layer,
  // independent of ScreenManager) picks up the new lock state on its own
  // next tick() and covers this screen.
}

void ScreenManager::onScanClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));

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
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_radius(overlay, 0, 0);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *label = lv_label_create(overlay);
  lv_label_set_text(label, "Scanning for changes\nin music library...");
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  // lv_refr_now(), not lv_timer_handler() -- see ScanProgressLabelListener's
  // comment above for why a reentrant lv_timer_handler() call from here
  // corrupts input state.
  lv_refr_now(nullptr);

  ScanProgressLabelListener progress(label);
  self->rescanner_.rescan(&progress);

  // Async, not lv_obj_del() -- we're still inside the click event that
  // LVGL's own (outer) lv_timer_handler() is currently dispatching;
  // deleting synchronously here risks the same input-state corruption
  // ScreenManager::render() already guards against for screen_ below.
  lv_obj_del_async(overlay);
  self->render();
}

}  // namespace knobify::ui
