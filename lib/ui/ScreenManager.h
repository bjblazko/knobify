#pragma once

#include <lvgl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "EdgeArc.h"
#include "FolderBrowser.h"
#include "InputRouter.h"
#include "LibraryRescanner.h"
#include "LibraryScanner.h"
#include "LockController.h"
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
                playback::PlaybackStateMachine &playback,
                power::LockController &lockController,
                library::LibraryRescanner &rescanner)
      : tabs_(tabs),
        library_(library),
        directoryReader_(directoryReader),
        playback_(playback),
        lockController_(lockController),
        rescanner_(rescanner) {}

  void begin();

  // Call after anything that might change what should be on screen:
  // a tap, a swipe, playback starting/stopping.
  void render();

  void onListMove(int16_t delta) override;

  // Cheap live update of the volume HUD (ring + number) on the Now
  // Playing screen (if that's what's currently shown) -- call after any
  // encoder tick that adjusted volume. Deliberately not a full render():
  // that would delete/recreate every widget on screen for what's often a
  // rapid sequence of small ticks. Shows the HUD and (re)starts its
  // auto-hide timer; call tickVolumeHud() every loop() to actually hide
  // it once the timeout elapses.
  void updateVolumeDisplay(uint32_t nowMs);

  // Hides the volume HUD once kVolumeHudTimeoutMs has passed since the
  // last updateVolumeDisplay() call. A no-op if the HUD isn't currently
  // showing. Call every loop() iteration, like updateElapsedTimeDisplay().
  void tickVolumeHud(uint32_t nowMs);

  // Cheap live update of the elapsed-time readout on the Now Playing
  // screen -- call every loop() iteration. A no-op if that screen isn't
  // currently shown or nothing is playing.
  void updateElapsedTimeDisplay();

 private:
  void renderList(const std::vector<std::pair<std::string, int>> &items,
                   bool showMiniBar);
  void renderNowPlaying();
  void renderBackButtonIfNeeded();
  void renderScanButtonIfNeeded();
  void applyHighlight();
  void goToNowPlaying();
  static std::string friendlyName(const std::string &path);

  static void onListItemClicked(lv_event_t *e);
  static void onBackClicked(lv_event_t *e);
  static void onMiniBarClicked(lv_event_t *e);
  static void onPrevClicked(lv_event_t *e);
  static void onPlayPauseClicked(lv_event_t *e);
  static void onNextClicked(lv_event_t *e);
  static void onLockClicked(lv_event_t *e);
  static void onScanClicked(lv_event_t *e);

  navigation::TabController &tabs_;
  library::LibraryIndex &library_;
  library::DirectoryReader &directoryReader_;
  playback::PlaybackStateMachine &playback_;
  power::LockController &lockController_;
  library::LibraryRescanner &rescanner_;

  static constexpr uint32_t kVolumeHudTimeoutMs = 3000;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *list_ = nullptr;
  lv_obj_t *miniBar_ = nullptr;
  lv_obj_t *elapsedLabel_ = nullptr;
  lv_obj_t *volumeArcHost_ = nullptr;
  lv_obj_t *volumeHudLabel_ = nullptr;
  ui_widgets::EdgeArc volumeArc_;
  bool volumeHudVisible_ = false;
  uint32_t volumeHudHideAtMs_ = 0;
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
