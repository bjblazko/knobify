#pragma once

#include <lvgl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FolderBrowser.h"
#include "InputRouter.h"
#include "LibraryScanner.h"
#include "PlaybackStateMachine.h"
#include "TabController.h"

namespace knobify::ui {

// Renders whatever screen TabController's active stack currently shows,
// and forwards taps into navigation/playback. Also implements
// ListMoveSink so InputRouter can move the highlighted list item on
// browse screens (see lib/input/InputRouter.h).
//
// Deliberately one file covering all screen kinds for this first cut,
// rather than splitting into ArtistsScreen/AlbumsScreen/etc. -- this UI
// hasn't been seen on real hardware yet, and splitting further before
// that happens would be premature. Revisit the split once the layout is
// validated on the device (round-display safe areas, touch target
// sizes, etc. per docs/adr/0004).
class ScreenManager : public input::ListMoveSink {
 public:
  ScreenManager(navigation::TabController &tabs,
                library::LibraryIndex &library,
                library::DirectoryReader &directoryReader,
                playback::PlaybackStateMachine &playback)
      : tabs_(tabs),
        library_(library),
        directoryReader_(directoryReader),
        playback_(playback) {}

  void begin();

  // Call after anything that might change what should be on screen:
  // a tap, a swipe, playback starting/stopping.
  void render();

  void onListMove(int16_t delta) override;

 private:
  void renderList(const std::vector<std::pair<std::string, int>> &items,
                   bool showMiniBar);
  void renderNowPlaying();
  void renderBackButtonIfNeeded();
  void applyHighlight();
  void goToNowPlaying();

  static void onListItemClicked(lv_event_t *e);
  static void onBackClicked(lv_event_t *e);
  static void onMiniBarClicked(lv_event_t *e);
  static void onPrevClicked(lv_event_t *e);
  static void onPlayPauseClicked(lv_event_t *e);
  static void onNextClicked(lv_event_t *e);

  navigation::TabController &tabs_;
  library::LibraryIndex &library_;
  library::DirectoryReader &directoryReader_;
  playback::PlaybackStateMachine &playback_;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *list_ = nullptr;
  lv_obj_t *miniBar_ = nullptr;
  int highlightedIndex_ = 0;

  // Kind IDs stashed on each clickable object via lv_obj_set_user_data so
  // the static click callbacks know what was tapped without capturing
  // C++ closures (LVGL v8 callbacks are plain C function pointers).
  struct ItemContext {
    ScreenManager *self;
    int index;
    library::TrackId trackId;
    library::AlbumId albumId;
    bool isFolder;
    std::string path;
  };
  // unique_ptr so addresses stay stable across vector growth -- LVGL
  // objects hold raw pointers to these as event user_data.
  std::vector<std::unique_ptr<ItemContext>> itemContexts_;
};

}  // namespace knobify::ui
