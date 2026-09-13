// Entry point and wiring only, per docs/coding-guidelines.md: construct
// concrete drivers and inject them into the logic layer.
#include <Arduino.h>
#include <SD_MMC.h>

#include <cstring>

#include "Cst816Driver.h"
#include "EncoderPins.h"
#include "Esp32AudioI2SDriver.h"
#include "GestureRecognizer.h"
#include "GpioEncoderDriver.h"
#include "IdleTimer.h"
#include "IndexCache.h"
#include "InputRouter.h"
#include "LibraryScanner.h"
#include "LockController.h"
#include "LockOverlay.h"
#include "LvglGlue.h"
#include "NvsKeyValueStore.h"
#include "PlaybackStateMachine.h"
#include "ScreenManager.h"
#include "SdDirectoryReader.h"
#include "SdFileLister.h"
#include "SdFileOpener.h"
#include "SdInit.h"
#include "St77916Driver.h"
#include "TabController.h"
#include "Version.h"
#include "VolumePersistence.h"

namespace {

// Where music lives on the SD card, and where the derived library index
// cache is persisted -- see decision 2, ADR 0004. Both are assumptions
// about SD card layout pending real-hardware verification.
constexpr const char *kMusicRoot = "/Music";
constexpr const char *kIndexCachePath = "/knobify/library.idx";

std::vector<uint8_t> readIndexCacheFile() {
  std::vector<uint8_t> bytes;
  fs::File file = SD_MMC.open(kIndexCachePath, FILE_READ);
  if (!file) return bytes;
  bytes.resize(file.size());
  file.read(bytes.data(), bytes.size());
  file.close();
  return bytes;
}

void writeIndexCacheFile(const std::vector<uint8_t> &bytes) {
  fs::File file = SD_MMC.open(kIndexCachePath, FILE_WRITE);
  if (!file) return;
  file.write(bytes.data(), bytes.size());
  file.close();
}

knobify::library::LibraryIndex loadOrBuildLibraryIndex(
    knobify::drivers::SdFileLister &lister, knobify::library::FileOpener &opener,
    knobify::library::ScanProgressListener *progress) {
  using knobify::library::IndexCache;
  using knobify::library::LibraryIndex;
  using knobify::library::LibrarySignature;
  using knobify::library::LibraryScanner;
  using knobify::library::computeSignature;

  LibrarySignature currentSignature = computeSignature(lister);

  LibraryIndex cachedIndex;
  LibrarySignature cachedSignature;
  std::vector<uint8_t> cacheBytes = readIndexCacheFile();
  if (!cacheBytes.empty() &&
      IndexCache::decode(cacheBytes, cachedIndex, cachedSignature) &&
      cachedSignature == currentSignature) {
    Serial.println("Library index cache is up to date; skipping rescan.");
    return cachedIndex;
  }

  Serial.println("Scanning SD card for library (cache missing/stale)...");
  LibraryIndex freshIndex = LibraryScanner::scan(lister, opener, progress);
  writeIndexCacheFile(IndexCache::encode(freshIndex, currentSignature));
  return freshIndex;
}

// Shows scan progress on-screen -- there's no other feedback on a device
// with no LEDs, and a slow or SD-error-prone scan (see AGENTS.md) could
// otherwise look identical to a dead board. Found from real hardware
// feedback 2026-09-12.
class BootProgressListener : public knobify::library::ScanProgressListener {
 public:
  explicit BootProgressListener(lv_obj_t *label) : label_(label) {}

  void onFileScanned(size_t filesScannedSoFar) override {
    char text[48];
    snprintf(text, sizeof(text), "Scanning library...\n%u files",
             static_cast<unsigned>(filesScannedSoFar));
    lv_label_set_text(label_, text);
    // Actually flushing to the panel on every single file would slow
    // the scan down for no real benefit -- every 5th file is still
    // clearly "moving" to a human, without adding meaningful overhead.
    if (filesScannedSoFar % 5 == 0) {
      lv_timer_handler();
    }
  }

  // TEMPORARY DIAGNOSTIC (2026-09-12): investigating "only a handful of
  // tracks found" reports on real hardware -- see AGENTS.md. Logs every
  // file the scanner processed, whether it opened, and what tags (if
  // any) came out, so a failure mode (can't open vs. opens but no tags
  // vs. tags found but grouped oddly) can be told apart from the serial
  // log alone. Remove once the SD reliability issue is resolved.
  void onFileResult(const std::string &path, bool opened,
                     const knobify::library::TagResult &tags) override {
    Serial.printf(
        "[scan] opened=%d artist=\"%s\" album=\"%s\" title=\"%s\" path=%s\n",
        opened, tags.artist.c_str(), tags.album.c_str(), tags.title.c_str(),
        path.c_str());
  }

 private:
  lv_obj_t *label_;
};

knobify::drivers::SdFileLister g_fileLister(kMusicRoot);
knobify::drivers::SdFileOpener g_fileOpener;
knobify::drivers::SdDirectoryReader g_directoryReader;
knobify::drivers::GpioEncoderDriver g_encoder(knobify::drivers::kEncoderPinA,
                                               knobify::drivers::kEncoderPinB);
knobify::drivers::NvsKeyValueStore g_nvsStore;
knobify::playback::VolumePersistence g_volume(g_nvsStore);
knobify::drivers::Esp32AudioI2SDriver g_audioDriver;
knobify::playback::PlaybackStateMachine g_playback(g_audioDriver, g_volume);
knobify::navigation::TabController g_tabs;

knobify::drivers::St77916Driver g_display;
knobify::drivers::Cst816Driver g_touch;
knobify::ui::LvglGlue g_lvglGlue;
knobify::library::LibraryIndex g_libraryIndex;
knobify::power::IdleTimer g_idleTimer;
knobify::power::LockController g_lockController;
knobify::ui::ScreenManager g_screenManager(g_tabs, g_libraryIndex,
                                            g_directoryReader, g_playback,
                                            g_lockController);
knobify::ui::LockOverlay g_lockOverlay(g_lockController);
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_screenManager);
knobify::input::GestureRecognizer g_gestureRecognizer;

bool g_wasPlaying = false;
bool g_backlightOn = true;
bool g_touchPressedPrev = false;
bool g_swallowingWakeTouch = false;

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("knobify %s starting\n", knobify::kVersion);

  g_encoder.begin();
  g_audioDriver.begin();
  // Must run after g_audioDriver.begin() (needs a real driver to push the
  // loaded volume into) -- see PlaybackStateMachine's constructor comment
  // for why this isn't done eagerly in the constructor itself.
  g_playback.begin();

  if (!g_touch.begin()) {
    Serial.println("Touch init FAILED -- check wiring/pinout in device.md");
  }

  // Display comes up FIRST, before the SD card is even touched, and
  // shows a boot/progress screen immediately -- this board has no LEDs,
  // so with the old ordering (display initialized only after the whole
  // library scan completed) a slow or SD-error-prone scan looked
  // indistinguishable from a dead board. Found from real hardware
  // feedback 2026-09-12; see AGENTS.md.
  lv_obj_t *bootScreen = nullptr;
  lv_obj_t *bootLabel = nullptr;
  bool displayOk = g_lvglGlue.begin(g_display);
  if (!displayOk) {
    Serial.println("Display init FAILED -- check wiring/pinout in device.md");
  } else {
    bootScreen = lv_obj_create(nullptr);
    lv_scr_load(bootScreen);
    lv_obj_t *title = lv_label_create(bootScreen);
    lv_label_set_text(title, "knobify");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);
    bootLabel = lv_label_create(bootScreen);
    lv_label_set_text(bootLabel, "Starting...");
    lv_obj_set_style_text_align(bootLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(bootLabel, LV_ALIGN_CENTER, 0, 20);
    lv_timer_handler();  // Flush immediately so something appears right away.
  }

  if (!knobify::drivers::initSdCard()) {
    Serial.println("SD card init FAILED -- check wiring/pinout in device.md");
    if (bootLabel) {
      lv_label_set_text(bootLabel, "SD card init FAILED");
      lv_timer_handler();
    }
  } else {
    BootProgressListener progressListener(bootLabel);
    g_libraryIndex = loadOrBuildLibraryIndex(
        g_fileLister, g_fileOpener, bootLabel ? &progressListener : nullptr);
    Serial.printf("Library: %u artists, %u albums, %u tracks\n",
                   static_cast<unsigned>(g_libraryIndex.artists.size()),
                   static_cast<unsigned>(g_libraryIndex.albums.size()),
                   static_cast<unsigned>(g_libraryIndex.tracks.size()));
  }

  if (displayOk) {
    g_screenManager.begin();
    // Created after the first screen so it's above it on LVGL's top
    // layer from the start -- see LockOverlay.h.
    g_lockOverlay.begin();
    if (bootScreen) lv_obj_del(bootScreen);
  }
}

// Diagnostic-only: a "SCREENSHOT\n" line over Serial dumps the current
// display contents (see LvglGlue::writeScreenshotToSerial(), decoded by
// scripts/screenshot.py into a BMP) -- lets a UI bug be diagnosed from an
// actual capture instead of a description or a phone photo.
void pollSerialCommands() {
  static char buf[16];
  static size_t len = 0;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        if (strcmp(buf, "SCREENSHOT") == 0) {
          g_lvglGlue.writeScreenshotToSerial();
        }
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void loop() {
  // Esp32AudioI2SDriver::loop() is a no-op: audio now services itself
  // continuously on its own FreeRTOS task (started in begin()),
  // decoupled from this loop's LVGL/input timing -- see that class's
  // comment for why. Still called through the interface for consistency
  // with PlaybackDriver's contract.
  g_audioDriver.loop();
  g_lvglGlue.pump();
  pollSerialCommands();

  uint32_t now = millis();
  int16_t encoderDelta = g_encoder.readDelta();

  // Touch is polled exactly once here and fanned out from this one
  // sample -- polling twice independently used to feed different
  // consumers slightly different coordinates (real capacitive touch
  // jitters between reads), which could make a single tap also register
  // as a swipe -- see LvglGlue.h.
  knobify::input::TouchSample touchSample{};
  g_touch.poll(touchSample);

  // Display power and lock are independent states (ADR 0005): any
  // activity resets the idle timer regardless of lock state, and the
  // very first touch after the display was off is swallowed entirely
  // below -- it only wakes the screen, never also acts on whatever it
  // landed on (a button on the table, or the unlock button in a pocket).
  bool displayWasOn = g_idleTimer.isDisplayOn();
  if (touchSample.pressed || encoderDelta != 0) {
    g_idleTimer.noteActivity(now);
  }
  bool displayOn = g_idleTimer.tick(now);
  if (displayOn != g_backlightOn) {
    g_display.setBacklight(displayOn ? 255 : 0);
    g_backlightOn = displayOn;
  }

  bool touchDownEdge = touchSample.pressed && !g_touchPressedPrev;
  g_touchPressedPrev = touchSample.pressed;
  if (touchDownEdge && !displayWasOn) {
    g_swallowingWakeTouch = true;
  }

  if (g_swallowingWakeTouch) {
    if (!touchSample.pressed) g_swallowingWakeTouch = false;
  } else {
    g_lvglGlue.feedTouch(touchSample);
    if (!g_lockController.isLocked()) {
      auto gesture = g_gestureRecognizer.feed(touchSample);
      if (gesture) {
        g_inputRouter.onGesture(*gesture);
        if (gesture->type == knobify::input::GestureType::SwipeLeftToRight) {
          g_screenManager.render();
        }
      }
    }
  }

  if (encoderDelta != 0) {
    if (g_lockController.isLocked()) {
      // While locked, the encoder only ever feeds the hold-and-turn
      // unlock gesture (LockController ignores deltas unless the
      // on-screen unlock button is currently held) -- no volume/list
      // passthrough while locked, per ADR 0005.
      g_lockController.onHoldEncoderDelta(encoderDelta);
    } else {
      g_inputRouter.onEncoderDelta(encoderDelta, now);
      // Cheap (no full re-render) so it can run on every tick -- see
      // ScreenManager::updateVolumeDisplay(). A no-op on any screen
      // other than Now Playing.
      g_screenManager.updateVolumeDisplay(now);
    }
  }
  g_screenManager.tickVolumeHud(now);

  g_lockOverlay.tick();

  g_playback.tick(now);
  // Cheap (no full re-render), a no-op on any screen other than Now
  // Playing -- see ScreenManager::updateElapsedTimeDisplay().
  g_screenManager.updateElapsedTimeDisplay();

  // Detect track-finished as a Playing->not-running transition. Pausing
  // also makes isRunning() report false, so this only applies while we
  // believe we're actively playing (not paused/stopped) -- verify this
  // against the installed ESP32-audioI2S version's actual pause behavior
  // once on hardware.
  bool isPlayingNow =
      g_playback.state() == knobify::playback::PlaybackState::Playing;
  if (g_wasPlaying && isPlayingNow && !g_audioDriver.isRunning()) {
    g_playback.onTrackFinished(millis());
    g_screenManager.render();
  }
  g_wasPlaying = isPlayingNow;
}
