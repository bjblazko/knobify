// Entry point and wiring only, per docs/coding-guidelines.md: construct
// concrete drivers and inject them into the logic layer.
#include <Arduino.h>
#include <SD_MMC.h>

#include "Cst816Driver.h"
#include "EncoderPins.h"
#include "Esp32AudioI2SDriver.h"
#include "GestureRecognizer.h"
#include "GpioEncoderDriver.h"
#include "IndexCache.h"
#include "InputRouter.h"
#include "LibraryScanner.h"
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
    knobify::drivers::SdFileLister &lister,
    knobify::library::FileOpener &opener) {
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
  LibraryIndex freshIndex = LibraryScanner::scan(lister, opener);
  writeIndexCacheFile(IndexCache::encode(freshIndex, currentSignature));
  return freshIndex;
}

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
knobify::ui::ScreenManager g_screenManager(g_tabs, g_libraryIndex,
                                            g_directoryReader, g_playback);
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_screenManager);
knobify::input::GestureRecognizer g_gestureRecognizer;

bool g_wasPlaying = false;

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("knobify %s starting\n", knobify::kVersion);

  if (!knobify::drivers::initSdCard()) {
    Serial.println("SD card init FAILED -- check wiring/pinout in device.md");
  } else {
    g_libraryIndex = loadOrBuildLibraryIndex(g_fileLister, g_fileOpener);
    Serial.printf("Library: %u artists, %u albums, %u tracks\n",
                   static_cast<unsigned>(g_libraryIndex.artists.size()),
                   static_cast<unsigned>(g_libraryIndex.albums.size()),
                   static_cast<unsigned>(g_libraryIndex.tracks.size()));
  }

  g_encoder.begin();
  g_audioDriver.begin();
  // Must run after g_audioDriver.begin() (needs a real driver to push the
  // loaded volume into) -- see PlaybackStateMachine's constructor comment
  // for why this isn't done eagerly in the constructor itself.
  g_playback.begin();

  if (!g_touch.begin()) {
    Serial.println("Touch init FAILED -- check wiring/pinout in device.md");
  }

  if (!g_lvglGlue.begin(g_display)) {
    Serial.println("Display init FAILED -- check wiring/pinout in device.md");
  } else {
    g_screenManager.begin();
  }
}

void loop() {
  g_audioDriver.loop();
  g_lvglGlue.pump();

  int16_t encoderDelta = g_encoder.readDelta();
  if (encoderDelta != 0) {
    g_inputRouter.onEncoderDelta(encoderDelta, millis());
    // Cheap (no full re-render) so it can run on every tick -- see
    // ScreenManager::updateVolumeDisplay(). A no-op on any screen other
    // than Now Playing.
    g_screenManager.updateVolumeDisplay();
  }

  // Touch is polled exactly once here and fed to both consumers --
  // LVGL's touch indev (via feedTouch(), for taps on widgets) and the
  // separate gesture recognizer (for the swipe-to-go-back/switch-tab
  // gesture, which LVGL widgets don't know about). Polling twice
  // independently used to feed each one a slightly different sample
  // (real capacitive touch coordinates jitter between reads), which
  // could make a single tap also register as a swipe -- see LvglGlue.h.
  knobify::input::TouchSample touchSample{};
  g_touch.poll(touchSample);
  g_lvglGlue.feedTouch(touchSample);
  auto gesture = g_gestureRecognizer.feed(touchSample);
  if (gesture) {
    g_inputRouter.onGesture(*gesture);
    if (gesture->type == knobify::input::GestureType::SwipeLeftToRight) {
      g_screenManager.render();
    }
  }

  g_playback.tick(millis());

  // Detect track-finished as a Playing->not-running transition. Pausing
  // also makes isRunning() report false, so this only applies while we
  // believe we're actively playing (not paused/stopped) -- verify this
  // against the installed ESP32-audioI2S version's actual pause behavior
  // once on hardware.
  bool isPlayingNow =
      g_playback.state() == knobify::playback::PlaybackState::Playing;
  if (g_wasPlaying && isPlayingNow && !g_audioDriver.isRunning()) {
    g_playback.onTrackFinished();
    g_screenManager.render();
  }
  g_wasPlaying = isPlayingNow;
}
