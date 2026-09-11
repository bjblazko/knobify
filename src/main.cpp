// Entry point and wiring only, per docs/coding-guidelines.md: construct
// concrete drivers and inject them into the logic layer.
//
// This wires up everything that doesn't need the display: SD card,
// library scan/cache, encoder, volume/NVS, and audio playback. Display,
// touch, and the LVGL screens (lib/ui/, lib/drivers-display/,
// lib/drivers-touch/) are NOT wired yet -- see
// docs/adr/0004-navigation-library-and-index-architecture.md's "display/
// touch bring-up" note. Until then there is no visible UI and no way to
// actually choose a track to play; this stage exists to get the
// lower-risk hardware pieces (SD, encoder, NVS, audio) verified on real
// hardware independently of the display work.
#include <Arduino.h>
#include <SD_MMC.h>

#include "EncoderPins.h"
#include "Esp32AudioI2SDriver.h"
#include "GpioEncoderDriver.h"
#include "IndexCache.h"
#include "InputRouter.h"
#include "LibraryScanner.h"
#include "NvsKeyValueStore.h"
#include "PlaybackStateMachine.h"
#include "SdFileLister.h"
#include "SdFileOpener.h"
#include "SdInit.h"
#include "TabController.h"
#include "Version.h"
#include "VolumePersistence.h"

namespace {

// Where music lives on the SD card, and where the derived library index
// cache is persisted -- see decision 2, ADR 0004. Both are assumptions
// about SD card layout pending real-hardware verification.
constexpr const char *kMusicRoot = "/Music";
constexpr const char *kIndexCachePath = "/knobify/library.idx";

// InputRouter needs a ListMoveSink to forward browse-screen scrolling
// to, but there's no list UI yet (lib/ui/ isn't built). This placeholder
// just swallows the intent until a real screen exists to consume it.
class NoOpListMoveSink : public knobify::input::ListMoveSink {
 public:
  void onListMove(int16_t) override {}
};

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
knobify::drivers::GpioEncoderDriver g_encoder(knobify::drivers::kEncoderPinA,
                                               knobify::drivers::kEncoderPinB);
knobify::drivers::NvsKeyValueStore g_nvsStore;
knobify::playback::VolumePersistence g_volume(g_nvsStore);
knobify::drivers::Esp32AudioI2SDriver g_audioDriver;
knobify::playback::PlaybackStateMachine g_playback(g_audioDriver, g_volume);
knobify::navigation::TabController g_tabs;
NoOpListMoveSink g_listSink;
knobify::input::InputRouter g_inputRouter(g_tabs, g_playback, g_listSink);

knobify::library::LibraryIndex g_libraryIndex;
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
}

void loop() {
  g_audioDriver.loop();

  int16_t encoderDelta = g_encoder.readDelta();
  if (encoderDelta != 0) {
    g_inputRouter.onEncoderDelta(encoderDelta, millis());
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
  }
  g_wasPlaying = isPlayingNow;

  // No display/touch yet -- see the file header comment. Once
  // lib/drivers-display/ and lib/ui/ exist, this loop also pumps
  // lv_timer_handler() and touch polling.
}
