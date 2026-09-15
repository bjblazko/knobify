#pragma once

#include <lvgl.h>

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "BrightnessSetting.h"
#include "CoverArtCache.h"
#include "DotMatrixSpectrum.h"
#include "EdgeArc.h"
#include "FolderBrowser.h"
#include "InputRouter.h"
#include "LibraryRescanner.h"
#include "LibraryScanner.h"
#include "KeyValueStore.h"
#include "LockController.h"
#include "MessageArea.h"
#include "PlaybackStateMachine.h"
#include "Shuttle.h"
#include "SpectrumAnalyzer.h"
#include "St77916Driver.h"
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
                playback::Shuttle &shuttle,
                power::LockController &lockController,
                library::LibraryRescanner &rescanner,
                library::CoverArtReader &coverReader,
                library::FileOpener &fileOpener,
                library::JpegDecoder &jpegDecoder,
                library::CoverWriter &coverWriter,
                playback::KeyValueStore &settings,
                power::BrightnessSetting &brightness,
                ui_widgets::MessageArea &messages)
      : tabs_(tabs),
        library_(library),
        directoryReader_(directoryReader),
        playback_(playback),
        shuttle_(shuttle),
        lockController_(lockController),
        rescanner_(rescanner),
        coverReader_(coverReader),
        fileOpener_(fileOpener),
        jpegDecoder_(jpegDecoder),
        coverWriter_(coverWriter),
        settings_(settings),
        brightness_(brightness),
        messages_(messages) {}

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

  // Cheap live update of the elapsed-time readout and song-progress ring
  // on the Now Playing screen -- call every loop() iteration. A no-op if
  // that screen isn't currently shown or nothing is playing.
  void updateElapsedTimeDisplay();

  // Advances the Now Playing spectrum (ADR 0009) at ~30 fps. A no-op
  // unless the spectrum is actually visible: another screen, the cover
  // shown instead, or `visible == false` (display off, locked) all skip
  // the FFT and every redraw. Call every loop() iteration.
  void tickSpectrum(uint32_t nowMs, bool visible);

  // Cheap live update of the Brightness screen's ring and percentage --
  // call after any encoder tick. A no-op on every other screen.
  void updateBrightnessDisplay();

 private:
  void renderList(const std::vector<std::pair<std::string, int>> &items,
                   bool showMiniBar);
  void renderMiniBar();
  void renderNowPlaying();
  // Main menu and settings -- ScreenManagerMenu.cpp (ADR 0010).
  void renderHome();
  void renderBrightness();
  void runRescan();
  void renderBackButtonIfNeeded();
  void renderContextCaption();
  void setProgressRingVisible(bool visible);
  // Shows/hides the top marker and shuttle arc for shownShuttle* (ADR 0013).
  void applyShuttleIndicator();
  static void onTimePillPressed(lv_event_t *e);
  static void onTimePillReleased(lv_event_t *e);
  void applyCoverSlotMode();
  // Tag metadata for a playing file, falling back to friendlyName() for
  // the title and empty strings otherwise (e.g. untagged Files-tab files).
  struct TrackInfo {
    std::string title;
    std::string artist;
    std::string album;
  };
  TrackInfo trackInfoFor(const std::string &path) const;
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
  static void onHomeTilePressed(lv_event_t *e);
  static void onHomeTileClicked(lv_event_t *e);
  static void onCoverSlotClicked(lv_event_t *e);
  // Feedback in the message area for what a toggle now does (ADR 0011).
  void showShuffleMessage();
  void showRepeatMessage();
  static void onShuffleClicked(lv_event_t *e);
  static void onRepeatClicked(lv_event_t *e);
  static bool hasShuffleRow(navigation::ScreenKind kind) {
    return kind == navigation::ScreenKind::Artists ||
           kind == navigation::ScreenKind::Albums ||
           kind == navigation::ScreenKind::Tracks;
  }

  navigation::TabController &tabs_;
  library::LibraryIndex &library_;
  library::DirectoryReader &directoryReader_;
  playback::PlaybackStateMachine &playback_;
  playback::Shuttle &shuttle_;
  power::LockController &lockController_;
  library::LibraryRescanner &rescanner_;
  library::CoverArtReader &coverReader_;
  library::FileOpener &fileOpener_;
  library::JpegDecoder &jpegDecoder_;
  library::CoverWriter &coverWriter_;
  playback::KeyValueStore &settings_;
  power::BrightnessSetting &brightness_;
  ui_widgets::MessageArea &messages_;

  static constexpr uint32_t kVolumeHudTimeoutMs = 3000;

  // Shared vertical rhythm (px from the top edge of the 360px frame) --
  // see docs/design/ux-guidelines.md §3a/§7. Header controls are >=44px
  // tall touch targets; the battery indicator aligns to their row.
  static constexpr lv_coord_t kHeaderButtonY = 6;
  static constexpr lv_coord_t kHeaderButtonW = 56;
  static constexpr lv_coord_t kHeaderButtonH = 44;
  static constexpr lv_coord_t kCaptionY = kHeaderButtonY + kHeaderButtonH + 2;
  static constexpr lv_coord_t kListTopY = kCaptionY + 22;
  static constexpr lv_coord_t kMiniBarZoneHeight = 88;
  static constexpr lv_coord_t kCoverY = 56;
  static constexpr lv_coord_t kTransportCenterY = 246;
  // Time pill (ADR 0013): between the shuffle/repeat toggles (x = ±100,
  // 44 px wide -> inner edges at ±78), leaving 12 px either side.
  static constexpr lv_coord_t kTimePillW = 132;
  static constexpr lv_coord_t kTimePillH = 28;
  static constexpr uint32_t kSpectrumFrameMs = 33;
  // Persisted cover-slot choice: 1 = spectrum, 0 = cover.
  static constexpr char kSpectrumSettingKey[] = "npSpectrum";
  // Persisted repeat mode (playback::RepeatMode). Shuffle isn't persisted:
  // it's set by how playback started (ADR 0011).
  static constexpr char kRepeatSettingKey[] = "repeat";
  // Now Playing's messages sit where the volume readout does: the center of
  // the cover slot, the calmest wide spot on the screen.
  static constexpr ui_widgets::MessageAnchor kNowPlayingMessageAnchor{
      drivers::kLcdHorRes / 2, kCoverY + 48};
  // Item id of the Shuffle row -- real ids are unsigned indices.
  static constexpr int kShuffleItemId = -1;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *list_ = nullptr;
  // Home's tile cells (one child per menu entry); the knob moves the
  // selection across them like across list rows.
  lv_obj_t *tiles_ = nullptr;
  // Remembered across renders so returning to Home keeps the tile you
  // left from selected.
  int homeSelection_ = 0;
  lv_obj_t *brightnessArcHost_ = nullptr;
  lv_obj_t *brightnessLabel_ = nullptr;
  ui_widgets::EdgeArc brightnessArc_;
  lv_obj_t *miniBar_ = nullptr;
  lv_obj_t *elapsedLabel_ = nullptr;
  lv_obj_t *coverImg_ = nullptr;
  // Own the decoded pixel buffer/descriptor as members (not locals in
  // renderNowPlaying()) since the lv_img object references them for as
  // long as it's on screen, well past that function returning.
  lv_img_dsc_t coverImgDsc_{};
  std::vector<uint16_t> coverPixels_;
  // The cover slot shows either the cover or the spectrum; always the
  // spectrum when there's no cover (user decision, ADR 0009).
  ui_widgets::DotMatrixSpectrum spectrum_;
  visualizer::SpectrumAnalyzer analyzer_;
  std::array<int16_t, visualizer::SpectrumAnalyzer::kFftSize> spectrumSamples_{};
  bool preferSpectrum_ = false;
  uint32_t lastSpectrumTickMs_ = 0;
  lv_obj_t *volumeArcHost_ = nullptr;
  lv_obj_t *volumeHudPill_ = nullptr;
  lv_obj_t *volumeHudLabel_ = nullptr;
  ui_widgets::EdgeArc volumeArc_;
  lv_obj_t *progressArcHost_ = nullptr;
  ui_widgets::EdgeArc progressArc_;
  // Null for tracks that can't shuttle -- then elapsedLabel_ is a plain label.
  lv_obj_t *timePill_ = nullptr;
  lv_obj_t *shuttleArcHost_ = nullptr;
  ui_widgets::EdgeArc shuttleArc_;
  lv_obj_t *shuttleMarker_ = nullptr;
  // What the pill text and arc currently show, to redraw only on change.
  bool shownShuttleHeld_ = false;
  int8_t shownShuttleStep_ = 0;
  // Decoder duration is fetched under the audio task's mutex, so it's
  // only re-queried when the displayed second changes, not every loop().
  int32_t lastShownSecond_ = -1;
  uint32_t durationSeconds_ = 0;
  bool volumeHudVisible_ = false;
  uint32_t volumeHudHideAtMs_ = 0;
  int highlightedIndex_ = 0;
  // Last rendered screen: a screen message is dismissed when this changes.
  navigation::ScreenKind renderedKind_ = navigation::ScreenKind::Home;

  // Kind IDs stashed on each clickable object via lv_obj_set_user_data so
  // the static click callbacks know what was tapped without capturing
  // C++ closures (LVGL v8 callbacks are plain C function pointers).
  struct ItemContext {
    ScreenManager *self;
    int index;
    library::TrackId trackId;
    library::AlbumId albumId;
    bool isFolder;
    bool isShuffle = false;
    std::string path;
  };
  // unique_ptr so addresses stay stable across vector growth -- LVGL
  // objects hold raw pointers to these as event user_data.
  std::vector<std::unique_ptr<ItemContext>> itemContexts_;
};

}  // namespace knobify::ui
