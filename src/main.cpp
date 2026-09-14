// Entry point and wiring only, per docs/coding-guidelines.md: construct
// concrete drivers and inject them into the logic layer.
#include <Arduino.h>
#include <SD_MMC.h>

#include <cstring>

#include "BatteryAdcDriver.h"
#include "BatteryIndicator.h"
#include "BatteryMonitor.h"
#include "CoverArtCache.h"
#include "Cst816Driver.h"
#include "EncoderPins.h"
#include "Esp32AudioI2SDriver.h"
#include "GestureRecognizer.h"
#include "GpioEncoderDriver.h"
#include "IdleTimer.h"
#include "IndexCache.h"
#include "InputRouter.h"
#include "LibraryRescanner.h"
#include "LibraryScanner.h"
#include "LockController.h"
#include "LockOverlay.h"
#include "LvglGlue.h"
#include "NvsKeyValueStore.h"
#include "PlaybackStateMachine.h"
#include "ScreenManager.h"
#include "SdCoverReader.h"
#include "SdCoverWriter.h"
#include "SdDirectoryReader.h"
#include "SdFileLister.h"
#include "SdFileOpener.h"
#include "SdInit.h"
#include "St77916Driver.h"
#include "TabController.h"
#include "TouchCalibration.h"
#include "TJpgDecoderAdapter.h"
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
  // The SD_MMC/FATFS layer refuses to create a file inside a directory
  // that doesn't exist yet -- kIndexCachePath's parent ("/knobify") is
  // never created anywhere else, so without this every single boot
  // silently failed to persist the cache and re-did the full scan from
  // scratch forever. mkdir() on an already-existing dir is a harmless
  // no-op. Found via live serial capture 2026-09-13 while investigating
  // slow boot -- see AGENTS.md.
  SD_MMC.mkdir("/knobify");
  fs::File file = SD_MMC.open(kIndexCachePath, FILE_WRITE);
  if (!file) {
    Serial.println("writeIndexCacheFile: could not open cache file for write");
    return;
  }
  file.write(bytes.data(), bytes.size());
  file.close();
}

// Forwards the usual scan-progress notifications to whatever listener the
// UI supplied, and additionally caches cover art the first time each
// album is scanned -- see LibraryScanner.h's onNewAlbum() and
// lib/library/CoverArtCache.h. Kept as a wrapper (rather than having
// ScreenManager's own listener do this) so cover-art caching stays a
// scan-level concern independent of whatever's showing progress on
// screen.
class CoverArtScanListener : public knobify::library::ScanProgressListener {
 public:
  CoverArtScanListener(knobify::library::ScanProgressListener *inner,
                        knobify::library::DirectoryReader &dirReader,
                        knobify::library::FileOpener &opener,
                        knobify::library::JpegDecoder &decoder,
                        knobify::library::CoverWriter &writer)
      : inner_(inner),
        dirReader_(dirReader),
        opener_(opener),
        decoder_(decoder),
        writer_(writer) {}

  void onFileScanned(size_t filesScannedSoFar) override {
    if (inner_) inner_->onFileScanned(filesScannedSoFar);
  }

  void onFileResult(const std::string &path, bool opened,
                     const knobify::library::TagResult &tags) override {
    if (inner_) inner_->onFileResult(path, opened, tags);
  }

  void onNewAlbum(const std::string &albumFolderPath,
                   knobify::library::RawFile &file,
                   const knobify::library::TagResult &tags) override {
    Serial.printf("[cover] onNewAlbum folder=%s picture.present=%d\n",
                  albumFolderPath.c_str(), tags.picture.present);
    knobify::library::CoverArtCache::ensureCoverCached(
        albumFolderPath, file, tags, dirReader_, opener_, decoder_, writer_);
    if (inner_) inner_->onNewAlbum(albumFolderPath, file, tags);
  }

 private:
  knobify::library::ScanProgressListener *inner_;
  knobify::library::DirectoryReader &dirReader_;
  knobify::library::FileOpener &opener_;
  knobify::library::JpegDecoder &decoder_;
  knobify::library::CoverWriter &writer_;
};

knobify::library::LibraryIndex loadOrBuildLibraryIndex(
    knobify::drivers::SdFileLister &lister, knobify::library::FileOpener &opener,
    knobify::library::DirectoryReader &coverDirReader,
    knobify::library::JpegDecoder &coverDecoder,
    knobify::library::CoverWriter &coverWriter,
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
  // computeSignature() above already walked `lister` to the end -- rewind
  // (not reset!) to replay those same entries instead of paying for a
  // second full SD directory walk.
  lister.rewind();
  CoverArtScanListener coverListener(progress, coverDirReader, opener,
                                      coverDecoder, coverWriter);
  LibraryIndex freshIndex = LibraryScanner::scan(lister, opener, &coverListener);
  writeIndexCacheFile(IndexCache::encode(freshIndex, currentSignature));
  return freshIndex;
}

// Concrete LibraryRescanner: wraps the SD-backed lister/opener (file-scope
// here, so ScreenManager/LibraryRescanner can't reference them directly)
// and calls the existing signature-check-then-scan logic on demand, from
// the library screen's scan button rather than at boot -- see AGENTS.md.
// Defined out-of-line below, once g_libraryIndex/g_fileLister/g_fileOpener
// exist.
class SdLibraryRescanner : public knobify::library::LibraryRescanner {
 public:
  void rescan(knobify::library::ScanProgressListener *progress) override;
};

knobify::drivers::SdFileLister g_fileLister(kMusicRoot);
knobify::drivers::SdFileOpener g_fileOpener;
SdLibraryRescanner g_libraryRescanner;
knobify::drivers::SdDirectoryReader g_directoryReader;
knobify::drivers::SdCoverWriter g_coverWriter;
knobify::drivers::SdCoverReader g_coverReader;
knobify::drivers::TJpgDecoderAdapter g_jpegDecoder;
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
knobify::ui::ScreenManager g_screenManager(
    g_tabs, g_libraryIndex, g_directoryReader, g_playback, g_lockController,
    g_libraryRescanner, g_coverReader, g_fileOpener, g_jpegDecoder,
    g_coverWriter, g_nvsStore);
knobify::ui::LockOverlay g_lockOverlay(g_lockController);
knobify::drivers::BatteryAdcDriver g_batteryAdc;
knobify::power::BatteryMonitor g_batteryMonitor;
knobify::ui::BatteryIndicator g_batteryIndicator(g_batteryMonitor);
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_screenManager);
knobify::input::GestureRecognizer g_gestureRecognizer;

bool g_wasPlaying = false;
bool g_backlightOn = true;
bool g_touchPressedPrev = false;
bool g_swallowingWakeTouch = false;
uint32_t g_lastBatteryUpdateMs = 0;
// Battery voltage moves slowly -- no need to re-read/re-render every
// loop() iteration like touch/encoder input does.
constexpr uint32_t kBatteryUpdateIntervalMs = 5000;

void SdLibraryRescanner::rescan(knobify::library::ScanProgressListener *progress) {
  g_libraryIndex = loadOrBuildLibraryIndex(g_fileLister, g_fileOpener,
                                            g_directoryReader, g_jpegDecoder,
                                            g_coverWriter, progress);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("knobify %s starting\n", knobify::kVersion);

  g_encoder.begin();
  g_batteryAdc.begin();
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
    // Boot no longer scans the SD card at all -- just loads whatever
    // library index was last cached (a single small file read, no
    // directory walk), so the device is usable immediately. Change
    // detection/full rescan now only happens on demand, via the scan
    // button on the library screen (ScreenManager::onScanClicked ->
    // SdLibraryRescanner::rescan()). If no cache exists yet (e.g. first
    // boot after flashing), the library just starts empty -- see
    // AGENTS.md.
    std::vector<uint8_t> cacheBytes = readIndexCacheFile();
    knobify::library::LibrarySignature ignoredSignature;
    if (!cacheBytes.empty() && knobify::library::IndexCache::decode(
                                    cacheBytes, g_libraryIndex, ignoredSignature)) {
      Serial.printf("Library: %u artists, %u albums, %u tracks (from cache)\n",
                     static_cast<unsigned>(g_libraryIndex.artists.size()),
                     static_cast<unsigned>(g_libraryIndex.albums.size()),
                     static_cast<unsigned>(g_libraryIndex.tracks.size()));
    } else {
      Serial.println("No library cache yet -- use the scan button to build one.");
    }
  }

  if (displayOk) {
    g_screenManager.begin();
    // Created after the first screen so it's above it on LVGL's top
    // layer from the start -- see LockOverlay.h.
    g_lockOverlay.begin();
    // Created after LockOverlay so it z-orders on top of it too -- the
    // lock screen is where battery status is always shown. See
    // BatteryIndicator.h.
    g_batteryIndicator.begin();
    g_batteryIndicator.update(g_batteryAdc.readMilliVolts());
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
#ifdef KNOBIFY_TOUCH_DEBUG
        if (strcmp(buf, "CALIB") == 0) {
          static const int kTargets[][2] = {
              {180, 180}, {90, 180}, {270, 180}, {180, 90}, {180, 270}};
          lv_obj_t *layer = lv_layer_top();
          for (const auto &t : kTargets) {
            lv_obj_t *h = lv_obj_create(layer);
            lv_obj_set_size(h, 30, 3);
            lv_obj_set_pos(h, t[0] - 15, t[1] - 1);
            lv_obj_set_style_bg_color(h, lv_color_hex(0xFF0000), 0);
            lv_obj_set_style_border_width(h, 0, 0);
            lv_obj_set_style_radius(h, 0, 0);
            lv_obj_t *v = lv_obj_create(layer);
            lv_obj_set_size(v, 3, 30);
            lv_obj_set_pos(v, t[0] - 1, t[1] - 15);
            lv_obj_set_style_bg_color(v, lv_color_hex(0xFF0000), 0);
            lv_obj_set_style_border_width(v, 0, 0);
            lv_obj_set_style_radius(v, 0, 0);
          }
          Serial.println("CALIB targets drawn");
        }
#endif
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
  touchSample = knobify::input::TouchCalibration::apply(touchSample);

#ifdef KNOBIFY_TOUCH_DEBUG
  {
    static uint32_t lastLoopMs = 0, maxLoopMs = 0, windowStartMs = 0;
    static uint32_t downAtMs = 0;
    static bool wasDown = false;
    if (lastLoopMs != 0 && now - lastLoopMs > maxLoopMs) {
      maxLoopMs = now - lastLoopMs;
    }
    lastLoopMs = now;
    if (touchSample.pressed != wasDown) {
      wasDown = touchSample.pressed;
      if (wasDown) downAtMs = now;
      Serial.printf("[touch] t=%lu %s (%d,%d)%s dur=%lums\n", now,
                    wasDown ? "down" : "up", touchSample.x, touchSample.y,
                    wasDown ? "" : "", wasDown ? 0UL : now - downAtMs);
    }
    if (now - windowStartMs >= 1000) {
      if (maxLoopMs > 40) {
        Serial.printf("[loop] maxLoop=%lums\n", maxLoopMs);
      }
      maxLoopMs = 0;
      windowStartMs = now;
    }
  }
#endif

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
  g_batteryIndicator.setLocked(g_lockController.isLocked());

  if (now - g_lastBatteryUpdateMs >= kBatteryUpdateIntervalMs) {
    g_lastBatteryUpdateMs = now;
    uint32_t batteryMilliVolts = g_batteryAdc.readMilliVolts();
    Serial.printf("[battery] %u mV\n", batteryMilliVolts);
    g_batteryIndicator.update(batteryMilliVolts);
  }

  g_playback.tick(now);
  // Cheap (no full re-render), a no-op on any screen other than Now
  // Playing -- see ScreenManager::updateElapsedTimeDisplay().
  g_screenManager.updateElapsedTimeDisplay();
  // ~30 fps while the Now Playing spectrum is on screen and actually seen;
  // skipped entirely otherwise (ADR 0009).
  g_screenManager.tickSpectrum(now, displayOn && !g_lockController.isLocked());

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
