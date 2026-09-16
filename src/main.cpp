// SPDX-License-Identifier: GPL-3.0-or-later
// Entry point and wiring only, per docs/coding-guidelines.md: construct
// concrete drivers and inject them into the logic layer.
#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <cstring>

#include "BatteryAdcDriver.h"
#include "BatteryIndicator.h"
#include "BatteryMonitor.h"
#include "BrightnessSetting.h"
#include "CoverArtCache.h"
#include "DeepSleep.h"
#include "Cst816Driver.h"
#include "EncoderPins.h"
#include "Esp32AudioI2SDriver.h"
#include "GestureRecognizer.h"
#include "GpioEncoderDriver.h"
#include "IdleTimer.h"
#include "IndexCache.h"
#include "InputRouter.h"
#include "JpegDecAdapter.h"
#include "LibraryRescanner.h"
#include "LibraryScanner.h"
#include "LockController.h"
#include "LockOverlay.h"
#include "LvglGlue.h"
#include "MusicResumeSource.h"
#include "NavigationResumeSource.h"
#include "NvsKeyValueStore.h"
#include "PlaybackStateMachine.h"
#include "ResumeScheduler.h"
#include "ScreenManager.h"
#include "SdCoverReader.h"
#include "SdCoverWriter.h"
#include "SdDirectoryReader.h"
#include "SdFileLister.h"
#include "SdFileOpener.h"
#include "SdInit.h"
#include "Shuttle.h"
#include "SleepTimer.h"
#include "St77916Driver.h"
#include "TabController.h"
#include "TouchCalibrator.h"
#include "Theme.h"
#include "UsbDriveSession.h"
#include "UsbMscStorage.h"
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
// Settings' "Rescan library" row rather than at boot -- see AGENTS.md.
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
knobify::drivers::JpegDecAdapter g_jpegDecoder;
knobify::drivers::GpioEncoderDriver g_encoder(knobify::drivers::kEncoderPinA,
                                               knobify::drivers::kEncoderPinB);
knobify::drivers::NvsKeyValueStore g_nvsStore;
knobify::playback::VolumePersistence g_volume(g_nvsStore);
knobify::drivers::Esp32AudioI2SDriver g_audioDriver;
knobify::playback::PlaybackStateMachine g_playback(g_audioDriver, g_volume);
knobify::playback::Shuttle g_shuttle(g_playback);
knobify::navigation::TabController g_tabs;
knobify::power::BrightnessSetting g_brightness(g_nvsStore);
knobify::power::SleepTimer g_sleepTimer;
knobify::input::TouchCalibrationFlow g_touchCalibration(g_nvsStore);
knobify::drivers::UsbMscStorage g_usbStorage;
knobify::usbdrive::UsbDriveSession g_usbDrive(g_usbStorage);

knobify::drivers::St77916Driver g_display;
knobify::drivers::Cst816Driver g_touch;
knobify::ui::LvglGlue g_lvglGlue;
knobify::library::LibraryIndex g_libraryIndex;
knobify::power::IdleTimer g_idleTimer;
knobify::power::LockController g_lockController;
knobify::ui_widgets::MessageArea g_messageArea;
knobify::ui::ScreenManager g_screenManager(
    g_tabs, g_libraryIndex, g_directoryReader, g_playback, g_shuttle,
    g_lockController, g_libraryRescanner, g_coverReader, g_fileOpener,
    g_jpegDecoder, g_coverWriter, g_nvsStore, g_brightness, g_sleepTimer,
    g_touchCalibration, g_usbDrive, g_messageArea);
knobify::ui::LockOverlay g_lockOverlay(g_lockController);
knobify::drivers::BatteryAdcDriver g_batteryAdc;
knobify::power::BatteryMonitor g_batteryMonitor;
knobify::ui::BatteryIndicator g_batteryIndicator(g_batteryMonitor);
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_shuttle,
                                          g_brightness, g_sleepTimer,
                                          g_touchCalibration, g_screenManager);
knobify::input::GestureRecognizer g_gestureRecognizer;

bool sdFileExists(const std::string &path) { return SD_MMC.exists(path.c_str()); }

// Where the device was before power went away (ADR 0012). Music restores
// before navigation, which drops Now Playing if no queue came back.
knobify::resume::MusicResumeSource g_musicResume(g_playback, g_libraryIndex,
                                                 &sdFileExists);
knobify::resume::NavigationResumeSource g_navigationResume(g_tabs, g_libraryIndex,
                                                           g_playback);
knobify::resume::ResumeScheduler g_resumeScheduler(
    g_nvsStore, {&g_musicResume, &g_navigationResume});

bool g_wasPlaying = false;
// Last duty written to the backlight PWM; LvglGlue::begin() leaves it at
// full (St77916Driver::initBacklight()).
uint8_t g_backlightDuty = 255;
bool g_touchPressedPrev = false;
bool g_swallowingWakeTouch = false;
// Whether the sleep timer's fade has started (its message shows once).
bool g_sleepFading = false;
uint32_t g_lastBatteryUpdateMs = 0;
// Battery voltage moves slowly -- no need to re-read/re-render every
// loop() iteration like touch/encoder input does.
constexpr uint32_t kBatteryUpdateIntervalMs = 5000;

void SdLibraryRescanner::rescan(knobify::library::ScanProgressListener *progress) {
  g_libraryIndex = loadOrBuildLibraryIndex(g_fileLister, g_fileOpener,
                                            g_directoryReader, g_jpegDecoder,
                                            g_coverWriter, progress);
}

// The sleep timer ran out (ADR 0015): keep what's worth keeping, then deep
// sleep until a touch. The fade has already brought the output to 0.
[[noreturn]] void enterSleepTimerDeepSleep(uint32_t now) {
  Serial.println("[sleep] timer expired -- entering deep sleep");
  if (g_playback.state() == knobify::playback::PlaybackState::Playing) {
    g_playback.togglePlayPause(now);
  }
  g_resumeScheduler.saveNow(now);
  // Volume and brightness changes still inside their save debounce.
  g_playback.tick(now + knobify::playback::PlaybackStateMachine::kVolumeSaveDebounceMs);
  g_brightness.tick(now + knobify::power::BrightnessSetting::kSaveDebounceMs);
  g_display.setBacklight(0);
  if (g_display.gfx()) g_display.gfx()->displayOff();
  g_touch.armWakeOnTouch();
  Serial.flush();
  knobify::drivers::enterDeepSleepUntilTouch();
}

}  // namespace

void setup() {
  // Small allocations (under ESP-IDF's default 4096 bytes) otherwise all go
  // to internal RAM, although ~8 MB of PSRAM is free: the library index's
  // and play queue's path strings filled it until the SD driver's DMA
  // buffers failed (ESP_ERR_NO_MEM, "sdmmc_read_blocks failed (257)") and
  // a library shuffle couldn't open its first track -- measured on the
  // device 2026-09-15: 38.9 KB internal free before, 1.7 KB after. Anything
  // needing internal/DMA memory asks for it explicitly via heap_caps.
  heap_caps_malloc_extmem_enable(32);
  // Before USB traffic can reach the drive (ADR 0016); asks for its DMA
  // buffer explicitly, so it isn't affected by the line above.
  g_usbStorage.begin();
  Serial.begin(115200);
  Serial.printf("knobify %s starting\n", knobify::kVersion);
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[sleep] woke from deep sleep by touch");
  }

  g_encoder.begin();
  g_batteryAdc.begin();
  g_audioDriver.begin();
  // Must run after g_audioDriver.begin() (needs a real driver to push the
  // loaded volume into) -- see PlaybackStateMachine's constructor comment
  // for why this isn't done eagerly in the constructor itself.
  g_playback.begin();
  // Otherwise every boot shuffles the same way (esp_random() is hardware
  // RNG, seeded from RF/bootloader entropy).
  g_playback.setRandomSeed(esp_random());
  g_brightness.begin();
  g_touchCalibration.begin();

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
  if (displayOk) {
    g_backlightDuty = g_brightness.duty();
    g_display.setBacklight(g_backlightDuty);
  }
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
      Serial.println("No library cache yet -- use Settings > Rescan library to build one.");
    }
  }

  // After a crash the saved state may be what caused it: start on Home once,
  // so a bad record can't trap the device in a reboot loop.
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
      resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT) {
    Serial.printf("[resume] discarded: reset reason %d\n", resetReason);
    g_resumeScheduler.discard(millis());
  } else if (g_resumeScheduler.restore(millis())) {
    Serial.println("[resume] restored");
  } else {
    Serial.println("[resume] nothing valid saved -- starting on Home");
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
    // Last on the top layer: messages show above everything (MessageArea.h).
    g_messageArea.begin(knobify::ui::theme::ink(), knobify::ui::theme::surface(),
                        knobify::drivers::kLcdHorRes);
    if (bootScreen) lv_obj_del(bootScreen);
  }
}

// A tap injected over Serial ("TAP x y", screen coordinates): held for
// kInjectedTapMs of loop() iterations in place of the real touch sample.
constexpr uint32_t kInjectedTapMs = 120;
uint32_t g_injectedTapUntilMs = 0;
int16_t g_injectedTapX = 0;
int16_t g_injectedTapY = 0;
// Knob detents injected over Serial ("KNOB n"), added to the next read.
int g_injectedDetents = 0;

// Diagnostic-only: a "SCREENSHOT\n" line over Serial dumps the current
// display contents (see LvglGlue::writeScreenshotToSerial(), decoded by
// scripts/screenshot.py into a BMP) -- lets a UI bug be diagnosed from an
// actual capture instead of a description or a phone photo. "TAP x y"
// taps the screen and "KNOB n" turns the knob n detents, so a flow can be
// driven without a hand on the device. "INFO" prints the reset reason:
// after a crash the TinyUSB serial port comes back too late to show the
// panic itself.
void pollSerialCommands() {
  static char buf[24];
  static size_t len = 0;
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        buf[len] = '\0';
        int x = 0;
        int y = 0;
        // Screenshots and INFO write a lot; not while the USB drive is
        // busy (UsbMscStorage::exporting()), where that would hang.
        if (strcmp(buf, "SCREENSHOT") == 0) {
          if (!knobify::drivers::UsbMscStorage::exporting()) {
            g_lvglGlue.writeScreenshotToSerial();
          }
        } else if (sscanf(buf, "TAP %d %d", &x, &y) == 2) {
          g_injectedTapX = static_cast<int16_t>(x);
          g_injectedTapY = static_cast<int16_t>(y);
          g_injectedTapUntilMs = millis() + kInjectedTapMs;
        } else if (strcmp(buf, "INFO") == 0 &&
                   !knobify::drivers::UsbMscStorage::exporting()) {
          // A reset reason of 4 is a panic: read the core dump (AGENTS.md).
          Serial.printf("[info] up %lus, reset reason %d, internal free %u, "
                        "loop stack left %u\n",
                        millis() / 1000, static_cast<int>(esp_reset_reason()),
                        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                        uxTaskGetStackHighWaterMark(nullptr));
        } else if (sscanf(buf, "KNOB %d", &x) == 1) {
          g_injectedDetents += x;
        }
        len = 0;
      }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

#ifdef KNOBIFY_LOOP_WDT
// Diagnostic build only (PLATFORMIO_BUILD_FLAGS=-DKNOBIFY_LOOP_WDT): turns
// a hung loop() into a panic, so the crash's core dump names the call it
// hung in (scripts/read-coredump.sh). That is how the USB CDC write spin
// was found twice (AGENTS.md).
//
// The timeout must clear the longest legitimate blocking call in loop(),
// which is a full library rescan: at 15 s it killed the scan itself
// (2026-09-16, ~1000 tracks). 90 s is comfortably past that and still
// catches a hang within a minute and a half.
#include <esp_task_wdt.h>
namespace {
constexpr int kLoopWatchdogSeconds = 90;

void armLoopWatchdog() {
  static bool armed = false;
  if (armed) return;
  armed = true;
  esp_task_wdt_init(kLoopWatchdogSeconds, true);
  esp_task_wdt_add(nullptr);
}
}  // namespace
#endif

void loop() {
#ifdef KNOBIFY_LOOP_WDT
  armLoopWatchdog();
  esp_task_wdt_reset();
#endif
  // Esp32AudioI2SDriver::loop() is a no-op: audio now services itself
  // continuously on its own FreeRTOS task (started in begin()),
  // decoupled from this loop's LVGL/input timing -- see that class's
  // comment for why. Still called through the interface for consistency
  // with PlaybackDriver's contract.
  g_audioDriver.loop();
  pollSerialCommands();

  uint32_t now = millis();
  int16_t encoderDelta = g_encoder.readDelta();
  encoderDelta += static_cast<int16_t>(g_injectedDetents);
  g_injectedDetents = 0;

  // Touch is polled exactly once here and fanned out from this one
  // sample -- polling twice independently used to feed different
  // consumers slightly different coordinates (real capacitive touch
  // jitters between reads), which could make a single tap also register
  // as a swipe -- see LvglGlue.h.
  knobify::input::TouchSample touchSample{};
  g_touch.poll(touchSample);
  // Kept raw for Settings > Touch calibration, which fits from raw points.
  const knobify::input::TouchSample rawTouchSample = touchSample;
  touchSample = g_touchCalibration.active().apply(rawTouchSample);
  if (g_injectedTapUntilMs != 0) {
    if (millis() < g_injectedTapUntilMs) {
      touchSample.pressed = true;
      touchSample.x = g_injectedTapX;
      touchSample.y = g_injectedTapY;
    } else {
      g_injectedTapUntilMs = 0;
    }
  }

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
  // A dark display only wakes from the knob after a deliberate quarter
  // turn, not from a nudge in a pocket (IdleTimer::kEncoderWakeDetents).
  bool displayWasOn = g_idleTimer.isDisplayOn();
  if (touchSample.pressed) g_idleTimer.noteActivity(now);
  if (encoderDelta != 0) g_idleTimer.noteEncoderDelta(encoderDelta, now);
  bool displayOn = g_idleTimer.tick(now);
  // Also follows the brightness setting live while it's being adjusted.
  uint8_t backlightDuty = displayOn ? g_brightness.duty() : 0;
  if (backlightDuty != g_backlightDuty) {
    g_display.setBacklight(backlightDuty);
    g_backlightDuty = backlightDuty;
  }
  // Nothing to render on a dark panel (this also stops the lock screen's
  // endless pulse animation). LVGL keeps its invalidated areas, so the
  // first pump after waking redraws the current state.
  if (displayOn) g_lvglGlue.pump();

  bool touchDownEdge = touchSample.pressed && !g_touchPressedPrev;
  g_touchPressedPrev = touchSample.pressed;
  if (touchDownEdge && !displayWasOn) {
    g_swallowingWakeTouch = true;
  }

  // Sleep timer (ADR 0015). During its fade a touch or (with the display
  // on) a turn means someone is still awake: cancel, and let that input do
  // nothing else. Not while locked -- that's a pocket.
  knobify::power::SleepPhase sleepPhase = g_sleepTimer.tick(now);
  if (sleepPhase == knobify::power::SleepPhase::Fading) {
    constexpr knobify::ui_widgets::MessageAnchor kCenter{
        knobify::drivers::kLcdHorRes / 2, knobify::drivers::kLcdVerRes / 2};
    bool stillAwake = !g_lockController.isLocked() &&
                      (touchDownEdge || (displayOn && encoderDelta != 0));
    if (stillAwake) {
      Serial.println("[sleep] cancelled during fade");
      g_sleepTimer.cancel();
      g_playback.setOutputGain(knobify::power::SleepTimer::kUnityGain);
      g_sleepFading = false;
      if (touchDownEdge) g_swallowingWakeTouch = true;
      encoderDelta = 0;
      g_messageArea.show("Sleep timer off", kCenter, now);
    } else {
      if (!g_sleepFading) {
        g_sleepFading = true;
        Serial.println("[sleep] fading out");
        g_messageArea.show("Going to sleep", kCenter, now);
      }
      g_playback.setOutputGain(g_sleepTimer.fadeGain(now));
    }
  } else if (sleepPhase == knobify::power::SleepPhase::Expired) {
    enterSleepTimerDeepSleep(now);
  } else if (g_sleepFading) {
    // Turned off or re-set on the Sleep screen mid-fade.
    g_sleepFading = false;
    g_playback.setOutputGain(knobify::power::SleepTimer::kUnityGain);
  }

  if (g_swallowingWakeTouch) {
    if (!touchSample.pressed) g_swallowingWakeTouch = false;
  } else if (g_touchCalibration.isCapturing()) {
    // Calibration taps go to the calibrator only: LVGL sees no touch (so
    // nothing underneath clicks) and no gesture can pop the screen.
    g_touchCalibration.feedRaw(rawTouchSample, now);
    g_lvglGlue.feedTouch(knobify::input::TouchSample{});
  } else {
    g_lvglGlue.feedTouch(touchSample);
    // Sideways drift while turning the knob during a shuttle hold
    // (ADR 0013) can register as a swipe and pop Now Playing right when
    // the user is mid-scrub -- skip recognizing/acting on gestures while
    // held, but keep feeding LVGL its touch above so the pill itself
    // still tracks the finger.
    if (!g_lockController.isLocked() && !g_shuttle.isHeld()) {
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
      // other than Now Playing, and skipped while the knob shuttles.
      if (!g_shuttle.isHeld()) g_screenManager.updateVolumeDisplay(now);
      g_screenManager.updateBrightnessDisplay();
    }
  }
  g_screenManager.tickVolumeHud(now);
  if (displayOn) g_screenManager.tickSleepTimer(now);

  // A calibration never survives the display going dark or the lock
  // screen: nobody is there to confirm it (TouchCalibrator.h).
  if (!displayOn || g_lockController.isLocked()) g_touchCalibration.cancel();
  g_screenManager.tickTouchCalibration(now);
  g_screenManager.tickUsbDrive(now);
  g_usbStorage.tickWrites();
  g_usbStorage.printEvents();
  // Serial commands are read, not written, so they stay available while a
  // USB drive session runs.

  // A shuttle hold ends with the finger (the pill's RELEASED/PRESS_LOST
  // normally does it; this also covers the pill being deleted by a
  // re-render mid-hold), or when its screen goes away (ADR 0013).
  if (g_shuttle.isHeld() &&
      (!touchSample.pressed || !displayOn || g_lockController.isLocked() ||
       g_tabs.activeStack().current().kind !=
           knobify::navigation::ScreenKind::NowPlaying)) {
    g_shuttle.release(now);
  }
  g_shuttle.tick(now);

  g_lockOverlay.tick();
  // A toggle's message means nothing on the lock screen.
  if (g_lockController.isLocked()) g_messageArea.dismissScreenMessage();
  g_messageArea.tick(now);
  g_batteryIndicator.setLocked(g_lockController.isLocked());

  if (now - g_lastBatteryUpdateMs >= kBatteryUpdateIntervalMs) {
    g_lastBatteryUpdateMs = now;
    uint32_t batteryMilliVolts = g_batteryAdc.readMilliVolts();
    // Never while the USB drive is busy -- see UsbMscStorage::exporting().
    if (!knobify::drivers::UsbMscStorage::exporting()) {
      Serial.printf("[battery] %u mV\n", batteryMilliVolts);
    }
    g_batteryIndicator.update(batteryMilliVolts);
  }

  g_playback.tick(now);
  g_resumeScheduler.tick(now);
  g_brightness.tick(now);
  // Cheap (no full re-render), a no-op on any screen other than Now
  // Playing -- see ScreenManager::updateElapsedTimeDisplay().
  if (displayOn) g_screenManager.updateElapsedTimeDisplay();
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

  // Don't busy-spin core 1: lets FreeRTOS idle the CPU between iterations.
  // Safe because the encoder counts in its ISR and short taps are latched
  // (TouchLatch); with the display off, only a wake touch needs catching.
  delay(displayOn ? 5 : 20);
}
