#include "UsbMscStorage.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <USBMSC.h>
#include <esp32-hal-tinyusb.h>
#include <esp_heap_caps.h>
#include <sdmmc_cmd.h>

#include <atomic>
#include <cstdio>
#include <cstring>

#include "SdInit.h"

namespace {

// Registers the MSC interface during static initialization, before the
// core calls USB.begin() (see UsbMscStorage.h).
USBMSC g_msc;

// TinyUSB hands over at most CONFIG_TINYUSB_MSC_BUFSIZE (4 KB) per call.
// Its buffer isn't guaranteed DMA-capable, which the SDMMC host needs for
// multi-sector transfers, so sectors go through this one.
constexpr size_t kBounceBytes = 4096;
uint8_t *g_bounce = nullptr;
sdmmc_card_t *g_card = nullptr;
std::atomic<bool> g_exported{false};
std::atomic<bool> g_ejectRequested{false};
// Sector calls running on TinyUSB's task, so stopExport() can wait for
// them before the card handle goes away.
std::atomic<int> g_inFlight{0};

// Counts a sector call for as long as it runs; `ok` is false once the
// export has ended. Counts before checking, so stopExport() can't miss it.
struct InFlight {
  InFlight() {
    ++g_inFlight;
    ok = g_exported.load();
  }
  ~InFlight() { --g_inFlight; }
  bool ok = false;
};

// Host events for printEvents(). Recorded as plain values: they happen on
// TinyUSB's task, whose 4 KB stack has no room for printf.
enum class Event : uint8_t { Export, Read, ReadRejected, ReadError, StartStop, CardBack, Host };
struct EventRecord {
  uint32_t ms;
  Event event;
  uint32_t a, b, c;
};
constexpr size_t kMaxEvents = 48;
EventRecord g_events[kMaxEvents];
size_t g_eventCount = 0;
portMUX_TYPE g_eventsLock = portMUX_INITIALIZER_UNLOCKED;
// The first reads of a session show how far a host's probe got.
constexpr int kReadsLogged = 12;
int g_readsLogged = 0;
std::atomic<uint64_t> g_bytesRead{0};
std::atomic<uint64_t> g_bytesWritten{0};

void noteEvent(Event event, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
  const EventRecord record{millis(), event, a, b, c};
  portENTER_CRITICAL(&g_eventsLock);
  if (g_eventCount < kMaxEvents) g_events[g_eventCount++] = record;
  portEXIT_CRITICAL(&g_eventsLock);
}

// SDMMCFS keeps the card handle to itself; the raw sector calls need it.
struct CardHandle : fs::SDMMCFS {
  static sdmmc_card_t *of(fs::SDMMCFS &fs) {
    return static_cast<CardHandle &>(fs)._card;
  }
};

int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size) {
  InFlight guard;
  if (g_readsLogged < kReadsLogged) {
    ++g_readsLogged;
    noteEvent(Event::Read, lba, offset, size);
  }
  if (!guard.ok || offset != 0 || size > kBounceBytes || size % 512 != 0) {
    noteEvent(Event::ReadRejected, lba, offset, size);
    return -1;
  }
  esp_err_t err = sdmmc_read_sectors(g_card, g_bounce, lba, size / 512);
  if (err != ESP_OK) {
    noteEvent(Event::ReadError, lba, size, static_cast<uint32_t>(err));
    return -1;
  }
  memcpy(buffer, g_bounce, size);
  g_bytesRead += size;
  return static_cast<int32_t>(size);
}

int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size) {
  InFlight guard;
  if (!guard.ok || offset != 0 || size > kBounceBytes || size % 512 != 0) {
    return -1;
  }
  memcpy(g_bounce, buffer, size);
  if (sdmmc_write_sectors(g_card, g_bounce, lba, size / 512) != ESP_OK) {
    return -1;
  }
  g_bytesWritten += size;
  return static_cast<int32_t>(size);
}

// Drops off the bus and comes back, like a USB stick being replugged. A
// medium that appears on an already-enumerated drive (no "medium changed"
// notice from this MSC stack) made macOS eject it at once (seen on the
// device 2026-09-16); a fresh enumeration is what hosts handle best.
void reenumerate() {
  tud_disconnect();
  delay(300);
  tud_connect();
}

bool onStartStop(uint8_t powerCondition, bool start, bool loadEject) {
  noteEvent(Event::StartStop, powerCondition, start, loadEject);
  if (loadEject && !start) g_ejectRequested = true;
  return true;
}

}  // namespace

namespace knobify::drivers {

void UsbMscStorage::begin() {
  g_bounce = static_cast<uint8_t *>(
      heap_caps_malloc(kBounceBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  g_msc.vendorID("knobify");
  g_msc.productID("Music");
  g_msc.productRevision("1.0");
  g_msc.onRead(onRead);
  g_msc.onWrite(onWrite);
  g_msc.onStartStop(onStartStop);
  g_msc.mediaPresent(false);
}

bool UsbMscStorage::startExport() {
  g_card = CardHandle::of(SD_MMC);
  if (!g_bounce || !g_card) return false;
  g_ejectRequested = false;
  g_exported = true;
  g_msc.begin(g_card->csd.capacity, 512);
  g_msc.mediaPresent(true);
  reenumerate();
  g_bytesRead = 0;
  g_bytesWritten = 0;
  g_readsLogged = 0;
  noteEvent(Event::Export, g_card->csd.capacity);
  return true;
}

void UsbMscStorage::stopExport() {
  g_msc.mediaPresent(false);
  g_exported = false;
  while (g_inFlight.load() > 0) delay(1);
  // The computer forgets the drive (after an eject it already has).
  reenumerate();
  // The FAT layer's cached tables are stale after the computer wrote.
  SD_MMC.end();
  bool mounted = initSdCard();
  noteEvent(Event::CardBack, static_cast<uint32_t>(g_bytesRead / 1024),
            static_cast<uint32_t>(g_bytesWritten / 1024), mounted);
}

bool UsbMscStorage::hostAttached() {
  const bool attached = tud_mounted() && !tud_suspended();
  static bool last = false;
  if (attached != last) {
    last = attached;
    noteEvent(Event::Host, attached);
  }
  return attached;
}

bool UsbMscStorage::takeEjectRequest() { return g_ejectRequested.exchange(false); }

bool UsbMscStorage::exporting() { return g_exported.load(); }

void UsbMscStorage::printEvents() {
  if (g_eventCount == 0 || !Serial || g_exported) return;
  EventRecord copy[kMaxEvents];
  portENTER_CRITICAL(&g_eventsLock);
  const size_t count = g_eventCount;
  memcpy(copy, g_events, count * sizeof(EventRecord));
  g_eventCount = 0;
  portEXIT_CRITICAL(&g_eventsLock);
  for (size_t i = 0; i < count; ++i) {
    const EventRecord &r = copy[i];
    Serial.printf("[usbdrive] t=%lu ", static_cast<unsigned long>(r.ms));
    switch (r.event) {
      case Event::Export: Serial.printf("exporting %u sectors\n", r.a); break;
      case Event::Read: Serial.printf("read lba=%u offset=%u size=%u\n", r.a, r.b, r.c); break;
      case Event::ReadRejected: Serial.printf("read REJECTED lba=%u offset=%u size=%u\n", r.a, r.b, r.c); break;
      case Event::ReadError: Serial.printf("read ERROR lba=%u size=%u err=0x%x\n", r.a, r.b, r.c); break;
      case Event::StartStop: Serial.printf("START STOP UNIT power=%u start=%u eject=%u\n", r.a, r.b, r.c); break;
      case Event::CardBack: Serial.printf("card back (read %u KB, written %u KB), remount %s\n", r.a, r.b, r.c ? "OK" : "FAILED"); break;
      case Event::Host: Serial.printf("host %s\n", r.a ? "attached" : "gone"); break;
    }
  }
}

}  // namespace knobify::drivers
