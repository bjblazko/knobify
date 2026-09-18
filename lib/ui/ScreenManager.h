#pragma once

#include <lvgl.h>

#include <array>
#include <cstdint>
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
#include "Bookmarks.h"
#include "CollectionSet.h"
#include "MenuVisibility.h"
#include "LetterJump.h"
#include "LibraryScanner.h"
#include "KeyValueStore.h"
#include "LockController.h"
#include "MessageArea.h"
#include "PlaybackStateMachine.h"
#include "BlipPlayer.h"
#include "TableTennisGame.h"
#include "SegmentDigits.h"
#include "Shuttle.h"
#include "SleepTimer.h"
#include "SpectrumAnalyzer.h"
#include "St77916Driver.h"
#include "TabController.h"
#include "TouchCalibrator.h"
#include "UsbDriveSession.h"

namespace knobify::ui {

// Renders whatever screen TabController's active stack currently shows,
// and forwards taps into navigation/playback. Also implements
// KnobSink so InputRouter can move the highlighted list item on
// browse screens (see lib/input/InputRouter.h).
//
// Deliberately one file covering all screen kinds for this first cut,
// rather than splitting into ArtistsScreen/AlbumsScreen/etc. -- this UI
// hasn't been seen on real hardware yet, and splitting further before
// that happens would be premature. Revisit the split once the layout is
// validated on the device (round-display safe areas, touch target
// sizes, etc. per docs/adr/0004).
class ScreenManager : public input::KnobSink {
 public:
  ScreenManager(navigation::TabController &tabs,
                collection::CollectionSet &collections,
                library::DirectoryReader &directoryReader,
                playback::PlaybackStateMachine &playback,
                playback::Shuttle &shuttle,
                power::LockController &lockController,
                library::CoverArtReader &coverReader,
                library::FileOpener &fileOpener,
                library::JpegDecoder &jpegDecoder,
                library::CoverWriter &coverWriter,
                playback::KeyValueStore &settings,
                power::BrightnessSetting &brightness,
                power::SleepTimer &sleepTimer,
                input::TouchCalibrationFlow &touchCalibration,
                usbdrive::UsbDriveSession &usbDrive,
                ui_widgets::MessageArea &messages,
                resume::Bookmarks &bookmarks)
      : tabs_(tabs),
        collections_(collections),
        directoryReader_(directoryReader),
        playback_(playback),
        shuttle_(shuttle),
        lockController_(lockController),
        coverReader_(coverReader),
        fileOpener_(fileOpener),
        jpegDecoder_(jpegDecoder),
        coverWriter_(coverWriter),
        settings_(settings),
        brightness_(brightness),
        sleepTimer_(sleepTimer),
        touchCalibration_(touchCalibration),
        usbDrive_(usbDrive),
        messages_(messages),
        bookmarks_(bookmarks),
        menuVisibility_(makeMenuVisibility()) {}

  void begin();

  // Re-roots Music's Library tab on the browse axis last chosen (ADR
  // 0021). Call before restoring a resume record: that restore only
  // accepts a saved stack whose root matches the tab's current one, so
  // the axis has to be in place first.
  void restoreBrowseAxis();

  // Call after anything that might change what should be on screen:
  // a tap, a swipe, playback starting/stopping.
  void render();

  void onListMove(int16_t delta) override;
  void onPaddleMove(int16_t delta) override;

  // Whether a swipe that began at this point belongs to a control rather
  // than to the screen behind it. The caption chip is up to 220px wide,
  // so a sloppy tap on it can drift far enough to be recognized as a
  // left-to-right swipe -- which at a stack root means "switch to the
  // Files tab". Tapping a control must never navigate somewhere else
  // (found on the device 2026-09-17). Same idea as the shuttle-held
  // guard in loop(): a finger on a control is not a swipe.
  bool swipeStartsOnControl(int16_t x, int16_t y) const;

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

  // Keeps the sleep timer's time left current (ADR 0015): the Sleep
  // screen's value and ring, and the Home tile's label. Redraws only when
  // the shown value changes. Call every loop().
  void tickSleepTimer(uint32_t nowMs);

  // Drives the Touch calibration screen: re-renders as targets are taken
  // or the phase changes, runs the Keep countdown, and leaves the screen
  // (with a message) once the flow ends -- however it ended. Also cancels
  // a flow whose screen was left some other way. Call every loop().
  void tickTouchCalibration(uint32_t nowMs);

  // Closes the header's letter-jump mode once it has been left alone
  // (ADR 0021). A no-op when it isn't open. Call every loop().
  void tickLetterJump(uint32_t nowMs);

  // Drives the USB drive screen (ADR 0016): follows the session's phase,
  // ends a session whose screen was left, and once a session ended -- by
  // eject, unplug or Done -- leaves the screen and rescans the library.
  // Call every loop().
  void tickUsbDrive(uint32_t nowMs);

  // Where a game's sounds go (ADR 0022). Optional: without one the games
  // are silent, which is all a host-side or audio-less build needs.
  void setBlipPlayer(games::BlipPlayer &blips) { blips_ = &blips; }

  // Advances Table Tennis by one frame (ADR 0022). Returns true while a rally is
  // actually running, which the caller turns into idle-timer activity:
  // nothing touches the knob or the screen during a long point, and a
  // dimmed display stops LVGL being pumped at all. Call every loop().
  bool tickTableTennis(uint32_t nowMs, bool visible);


 private:
  void renderList(const std::vector<std::pair<std::string, int>> &items,
                   bool showMiniBar);
  // The sort keys of a list's data rows, folded exactly the way that list
  // is ordered -- what the header's letter buckets are built from.
  std::vector<std::string> letterKeysFor(
      const std::vector<std::pair<std::string, int>> &items,
      int leadingRows) const;
  // Whether this screen's rows are in an order letters can be read from
  // at all: a Tracks list is in track-number order, Music's albums in
  // year order, so neither offers it (ADR 0021).
  bool listIsNameOrdered() const;
  const char *emptyListText(const navigation::Screen &screen) const;
  // The caption chip is a hold button, not a click one: pressing opens
  // the letter mode straight away, so holding it and turning the knob
  // with the other hand works -- the same two-handed gesture the time
  // pill's shuttle uses (ADR 0013). Releasing leaves it open, so a plain
  // tap works too; a second press closes it.
  static void onCaptionPressed(lv_event_t *e);
  static void onCaptionReleased(lv_event_t *e);
  // Text and colours of the existing chip, without deleting it. A press
  // must not destroy the very object the touch is tracking: the finger
  // is still down on it, and its release would never arrive.
  void applyCaptionChip();
  std::string captionTextFor(const navigation::Screen &screen) const;
  // The albums an AlbumsFlat screen shows: every one, or the year's, or
  // the genre's -- whichever its params ask for.
  std::vector<library::AlbumId> shelfAlbums(
      const navigation::Screen &screen) const;
  std::string artistNameForRow(navigation::ScreenKind kind, int itemId) const;
  void renderMiniBar();
  void renderNowPlaying();
  // Main menu and settings -- ScreenManagerMenu.cpp (ADR 0010).
  void renderHome();
  void renderWordmark();
  void renderBrightness();
  void renderSleepTimer();
  void renderTouchCalibration();
  // ScreenManagerGames.cpp.
  void renderTableTennis();
  void applyTableTennisScene();
  void drainTableTennisSounds();
  static void onTableTennisTapped(lv_event_t *e);
  static void onCalibrationKeepClicked(lv_event_t *e);
  void startUsbDrive();
  void renderUsbDrive();
  static void onUsbDriveDoneClicked(lv_event_t *e);
  // Rebuilds one collection's index, or every one in turn, behind a
  // blocking progress overlay. Both are multi-second and synchronous --
  // see runRescan()'s comment in ScreenManager.cpp.
  void runRescan(collection::CollectionId id);
  void runRescanAll();
  // Settings > Main menu (ADR 0018): which destinations Home shows.
  void appendMenuVisibilityRows(
      std::vector<std::pair<std::string, int>> &items) const;
  void toggleMenuEntryVisible(int entryIndex);
  bool menuEntryVisible(int entryIndex) const;
  const char *menuVisibilityValue(int entryIndex) const;
  void loadMenuVisibility();
  // Built from the menu table, which lives in ScreenManagerMenu.cpp.
  static navigation::MenuVisibility makeMenuVisibility();
  // Indices into the menu table, filtered by the visibility setting -- the
  // carousel's order (ADR 0018).
  std::vector<int> visibleMenuEntries() const;
  void moveHomeSelection(int delta);

  // One Settings row. A table rather than a chain of index comparisons,
  // so adding a row never silently renumbers the ones after it.
  struct SettingsRow {
    const char *label;
    void (*open)(ScreenManager &self);
  };
  static constexpr int kSettingsRowCount = 5;
  static const SettingsRow kSettingsRows[kSettingsRowCount];
  // Music's browse axes (ADR 0021): which shelf the Library tab is
  // rooted on, remembered across reboots.
  struct BrowseAxis {
    const char *label;
    navigation::ScreenKind kind;
  };
  static constexpr int kBrowseAxisCount = 5;
  static const BrowseAxis kBrowseAxes[kBrowseAxisCount];
  // Whether this screen leads with the "Browse by" row: only Music, and
  // only on the shelf's own root -- an artist's albums are not a shelf.
  bool hasBrowseAxisRow(const navigation::Screen &screen) const;
  void openBrowseAxis(int axisIndex);

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
  // `slot` is a TileSlot (ScreenManagerMenu.cpp, where the carousel's
  // geometry lives): -1 left, 0 centre, 1 right.
  void makeMenuTile(int entryIndex, int slot);
  static void onHomeTileClicked(lv_event_t *e);
  // Options panel on Now Playing (ADR 0014).
  void renderOptionsPanel(bool animate);
  static void onCoverSwitchClicked(lv_event_t *e);
  static void onOptionsHandleClicked(lv_event_t *e);
  static void onOptionsPanelCloseClicked(lv_event_t *e);
  // Feedback in the message area for what a toggle now does (ADR 0011).
  void showShuffleMessage();
  void showRepeatMessage();
  static void onShuffleClicked(lv_event_t *e);
  static void onRepeatClicked(lv_event_t *e);
  // Whether a list screen leads with a Shuffle row. Spoken-word
  // collections never do: shuffling an audiobook's chapters is never what
  // anyone wants (ADR 0018), so this asks the collection's profile as
  // well as the screen kind.
  // Whether a Tracks list leads with a Continue row: a spoken-word title
  // with a saved position somewhere in it (ADR 0018). Fills `out` with
  // that bookmark.
  bool hasContinueRow(const navigation::Screen &screen,
                      resume::Bookmark &out) const;
  // The folder holding an album's tracks -- the bookmark key for a title.
  std::string titleKeyFor(library::AlbumId albumId) const;
  void playFromBookmark(const resume::Bookmark &mark, library::AlbumId albumId);

  bool hasShuffleRow(navigation::ScreenKind kind) const {
    return profile().hasShuffleRow &&
           (kind == navigation::ScreenKind::Artists ||
            kind == navigation::ScreenKind::Albums ||
            kind == navigation::ScreenKind::Tracks ||
            // Music's other shelves shuffle what they show (ADR 0021).
            // Years does not: a list of years is not a shelf of music,
            // it is the way to one.
            kind == navigation::ScreenKind::AlbumsFlat ||
            kind == navigation::ScreenKind::Songs ||
            kind == navigation::ScreenKind::Genres);
  }

  navigation::TabController &tabs_;
  // The collection the active screen belongs to, its profile, and its
  // index. Every browse screen carries its collection in ScreenParams, so
  // these three follow the navigation stack rather than any mode state
  // ScreenManager would have to keep in sync (ADR 0018). On screens with
  // no collection of their own (Home, Settings) they answer for Music,
  // which nothing on those screens reads.
  collection::CollectionId currentCollection() const;
  const collection::CollectionProfile &profile() const;
  const collection::CollectionProfile &playingProfile() const;
  library::LibraryIndex &library();
  const library::LibraryIndex &library() const {
    return const_cast<ScreenManager *>(this)->library();
  }

  collection::CollectionSet &collections_;
  library::DirectoryReader &directoryReader_;
  playback::PlaybackStateMachine &playback_;
  playback::Shuttle &shuttle_;
  power::LockController &lockController_;
  library::CoverArtReader &coverReader_;
  library::FileOpener &fileOpener_;
  library::JpegDecoder &jpegDecoder_;
  library::CoverWriter &coverWriter_;
  playback::KeyValueStore &settings_;
  power::BrightnessSetting &brightness_;
  power::SleepTimer &sleepTimer_;
  input::TouchCalibrationFlow &touchCalibration_;
  usbdrive::UsbDriveSession &usbDrive_;
  ui_widgets::MessageArea &messages_;
  resume::Bookmarks &bookmarks_;

  static constexpr uint32_t kVolumeHudTimeoutMs = 3000;

  // Shared vertical rhythm (px from the top edge of the 360px frame) --
  // see docs/design/ux-guidelines.md §3a/§7. Header controls are >=44px
  // tall touch targets; the battery indicator aligns to their row.
  static constexpr lv_coord_t kHeaderButtonY = 6;
  static constexpr lv_coord_t kHeaderButtonW = 56;
  static constexpr lv_coord_t kHeaderButtonH = 44;
  static constexpr lv_coord_t kCaptionY = kHeaderButtonY + kHeaderButtonH + 2;
  // The caption doubles as the letter-jump chip (ADR 0021). It has to
  // stay inside the 22px band between the header buttons and the list --
  // kListTopY does not move for it.
  static constexpr lv_coord_t kCaptionChipH = 20;
  static constexpr lv_coord_t kCaptionChipPadX = 10;
  static constexpr lv_coord_t kCaptionChipMaxW = 220;
  static constexpr lv_coord_t kListTopY = kCaptionY + 22;
  static constexpr lv_coord_t kMiniBarZoneHeight = 88;
  static constexpr lv_coord_t kCoverY = 44;
  static constexpr lv_coord_t kTransportCenterY = 236;
  // Time pill (ADR 0013), alone on its row since ADR 0014: 14 px below
  // Play/Pause (bottom edge kTransportCenterY + 36). Cover, titles and
  // transport moved up (kCoverY 56 -> 44, kTransportCenterY 246 -> 236) to
  // give the pill and the options handle room -- taps kept landing on the
  // wrong control (user feedback 2026-09-15).
  static constexpr lv_coord_t kTimePillW = 132;
  static constexpr lv_coord_t kTimePillH = 28;
  static constexpr lv_coord_t kTimePillY = kTransportCenterY + 36 + 14;
  // Options handle (ADR 0014): a short target whose chevron sits about as
  // far from the bottom edge as the back chevron from the top.
  static constexpr lv_coord_t kOptionsHandleH = 24;
  static constexpr lv_coord_t kOptionsHandleBottom = 12;
  // Top edge of the options sheet: just below the cover slot, so messages
  // and the volume readout (centered on the slot) stay visible above it.
  static constexpr lv_coord_t kOptionsPanelY = 146;
  static constexpr uint32_t kOptionsPanelAnimMs = 200;
  // How long a shuttle hint/speed message may stay while the pill is held.
  static constexpr uint32_t kShuttleHintMs = 10000;
  static constexpr uint32_t kSpectrumFrameMs = 33;
  // The game redraws at the same rate as the spectrum: enough for a ball that
  // crosses the court in a second or two, and cheap enough that the audio
  // decoder keeps its share of the loop.
  static constexpr uint32_t kTableTennisFrameMs = 33;
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
  static constexpr int kContinueItemId = -2;
  static constexpr int kBrowseAxisItemId = -3;
  // Persisted Music browse axis (navigation::ScreenKind by value).
  static constexpr char kMusicAxisKey[] = "musicAxis";
  // Bit per main-menu entry, 1 = shown. NVS keys are limited to 15
  // characters. Default: everything visible, which is what a device that
  // has never opened this screen should look like.
  static constexpr char kMenuVisibilityKey[] = "menuVis";
  // Constructed in ScreenManagerMenu.cpp, where the menu table (and so the
  // entry count and which entry is pinned) lives.
  navigation::MenuVisibility menuVisibility_;

  lv_obj_t *screen_ = nullptr;
  lv_obj_t *list_ = nullptr;
  // The context caption, which doubles as the letter-jump target on long
  // name-ordered lists (ADR 0021).
  lv_obj_t *caption_ = nullptr;
  navigation::LetterJump letterJump_;
  // Whether the press currently on the chip is the one that opened the
  // mode: if it is, releasing keeps it open (a hold, or a tap that will
  // be followed by a turn); if not, the press was a second tap and
  // releasing closes it.
  bool captionPressOpened_ = false;
  bool captionPressed_ = false;
  // Home's tile cells (one child per menu entry); the knob moves the
  // selection across them like across list rows.
  lv_obj_t *tiles_ = nullptr;
  // Remembered across renders so returning to Home keeps the tile you
  // left from selected.
  int homeSelection_ = 0;
  lv_obj_t *brightnessArcHost_ = nullptr;
  lv_obj_t *brightnessLabel_ = nullptr;
  ui_widgets::EdgeArc brightnessArc_;
  // The Sleep screen's value and ring, and the Home tile label that shows
  // the time left; what they show, to redraw only on change.
  lv_obj_t *sleepArcHost_ = nullptr;
  lv_obj_t *sleepValueLabel_ = nullptr;
  ui_widgets::EdgeArc sleepArc_;
  lv_obj_t *sleepTileLabel_ = nullptr;
  uint32_t shownSleepMinutes_ = UINT32_MAX;
  uint32_t shownSleepSeconds_ = UINT32_MAX;
  // What the Touch calibration screen currently shows, to re-render only
  // on change.
  input::CalibrationPhase shownCalibrationPhase_ = input::CalibrationPhase::Idle;
  size_t shownCalibrationTargets_ = 0;
  bool shownCalibrationRejected_ = false;
  // What the USB drive screen shows, to redraw only on change.
  usbdrive::UsbDrivePhase shownUsbDrivePhase_ = usbdrive::UsbDrivePhase::Off;
  lv_obj_t *calibrationArcHost_ = nullptr;
  ui_widgets::EdgeArc calibrationArc_;
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

  // Table Tennis (ADR 0022). The game itself is pure logic; these are the six
  // rectangles and two segment displays it is drawn with.
  games::TableTennisGame tableTennis_;
  lv_obj_t *tableTennisBall_ = nullptr;
  lv_obj_t *tableTennisPlayerPaddle_ = nullptr;
  lv_obj_t *tableTennisAiPaddle_ = nullptr;
  lv_obj_t *tableTennisHint_ = nullptr;
  ui_widgets::SegmentDigits tableTennisPlayerScore_;
  ui_widgets::SegmentDigits tableTennisAiScore_;
  games::BlipPlayer *blips_ = nullptr;
  games::TableTennisGame::Phase shownTableTennisPhase_ = games::TableTennisGame::Phase::Ready;
  uint32_t lastTableTennisTickMs_ = 0;
  lv_obj_t *volumeArcHost_ = nullptr;
  lv_obj_t *volumeHudPill_ = nullptr;
  lv_obj_t *volumeHudLabel_ = nullptr;
  ui_widgets::EdgeArc volumeArc_;
  lv_obj_t *progressArcHost_ = nullptr;
  ui_widgets::EdgeArc progressArc_;
  // Null for tracks that can't shuttle -- then elapsedLabel_ is a plain label.
  lv_obj_t *timePill_ = nullptr;
  // Options panel state survives render() (its toggles re-render the
  // screen); closed when the screen changes.
  bool optionsPanelOpen_ = false;
  bool animateOptionsPanel_ = false;
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
    bool isContinue = false;
    bool isBrowseAxis = false;
    std::string path;
  };
  // unique_ptr so addresses stay stable across vector growth -- LVGL
  // objects hold raw pointers to these as event user_data.
  std::vector<std::unique_ptr<ItemContext>> itemContexts_;
};

}  // namespace knobify::ui
