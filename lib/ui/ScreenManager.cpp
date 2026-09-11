#include "ScreenManager.h"

#include <algorithm>

#include "St77916Driver.h"

using knobify::navigation::Screen;
using knobify::navigation::ScreenKind;
using knobify::navigation::ScreenParams;

namespace knobify::ui {


void ScreenManager::begin() { render(); }

void ScreenManager::render() {
  itemContexts_.clear();
  highlightedIndex_ = 0;

  if (screen_) {
    lv_obj_del(screen_);
  }
  screen_ = lv_obj_create(nullptr);
  lv_scr_load(screen_);
  list_ = nullptr;
  miniBar_ = nullptr;

  Screen current = tabs_.activeStack().current();

  if (current.kind == ScreenKind::NowPlaying) {
    renderNowPlaying();
    return;
  }

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
      auto entries =
          library::FolderBrowser::list(directoryReader_, current.params.folderPath);
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

void ScreenManager::renderList(
    const std::vector<std::pair<std::string, int>> &items, bool showMiniBar) {
  Screen current = tabs_.activeStack().current();
  list_ = lv_list_create(screen_);
  lv_obj_set_size(list_, drivers::kLcdHorRes,
                   showMiniBar ? drivers::kLcdVerRes - 40 : drivers::kLcdVerRes);
  lv_obj_align(list_, LV_ALIGN_TOP_MID, 0, 0);

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
    miniBar_ = lv_obj_create(screen_);
    lv_obj_set_size(miniBar_, drivers::kLcdHorRes, 40);
    lv_obj_align(miniBar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *label = lv_label_create(miniBar_);
    lv_label_set_text(label, playback_.currentPath().c_str());
    lv_obj_center(label);
    lv_obj_add_flag(miniBar_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(miniBar_, &ScreenManager::onMiniBarClicked,
                         LV_EVENT_CLICKED, this);
  }

  applyHighlight();
}

void ScreenManager::renderNowPlaying() {
  lv_obj_t *label = lv_label_create(screen_);
  lv_label_set_text(label, playback_.currentPath().c_str());
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_set_width(label, drivers::kLcdHorRes - 80);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);

  // Buttons sit inset from the edges, safely within the round display's
  // visible area rather than at the literal corners -- see decision 6,
  // ADR 0004.
  lv_obj_t *prevBtn = lv_btn_create(screen_);
  lv_obj_set_size(prevBtn, 70, 70);
  lv_obj_align(prevBtn, LV_ALIGN_CENTER, -90, 60);
  lv_obj_t *prevLabel = lv_label_create(prevBtn);
  lv_label_set_text(prevLabel, LV_SYMBOL_PREV);
  lv_obj_center(prevLabel);
  lv_obj_add_event_cb(prevBtn, &ScreenManager::onPrevClicked, LV_EVENT_CLICKED,
                       this);

  lv_obj_t *playPauseBtn = lv_btn_create(screen_);
  lv_obj_set_size(playPauseBtn, 80, 80);
  lv_obj_align(playPauseBtn, LV_ALIGN_CENTER, 0, 60);
  lv_obj_t *playPauseLabel = lv_label_create(playPauseBtn);
  lv_label_set_text(playPauseLabel,
                     playback_.state() == playback::PlaybackState::Playing
                         ? LV_SYMBOL_PAUSE
                         : LV_SYMBOL_PLAY);
  lv_obj_center(playPauseLabel);
  lv_obj_add_event_cb(playPauseBtn, &ScreenManager::onPlayPauseClicked,
                       LV_EVENT_CLICKED, this);

  lv_obj_t *nextBtn = lv_btn_create(screen_);
  lv_obj_set_size(nextBtn, 70, 70);
  lv_obj_align(nextBtn, LV_ALIGN_CENTER, 90, 60);
  lv_obj_t *nextLabel = lv_label_create(nextBtn);
  lv_label_set_text(nextLabel, LV_SYMBOL_NEXT);
  lv_obj_center(nextLabel);
  lv_obj_add_event_cb(nextBtn, &ScreenManager::onNextClicked, LV_EVENT_CLICKED,
                       this);
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
      self->playback_.play(playlist, startIndex);
      self->goToNowPlaying();
      break;
    }
    case ScreenKind::Folder:
      if (ctx->isFolder) {
        self->tabs_.activeStack().push(
            Screen{ScreenKind::Folder, ScreenParams{.folderPath = ctx->path}});
        self->render();
      } else {
        self->playback_.play({ctx->path}, 0);
        self->goToNowPlaying();
      }
      break;
    default:
      break;
  }
}

void ScreenManager::onMiniBarClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->goToNowPlaying();
}

void ScreenManager::onPrevClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.prev();
  self->render();
}

void ScreenManager::onPlayPauseClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.togglePlayPause();
  self->render();
}

void ScreenManager::onNextClicked(lv_event_t *e) {
  auto *self = static_cast<ScreenManager *>(lv_event_get_user_data(e));
  self->playback_.next();
  self->render();
}

}  // namespace knobify::ui
