// Throwaway feasibility firmware (not part of the app): USB mass storage
// export of the SD card, AAC/M4A/FLAC playback with CPU and underrun
// measurement, progressive JPEG cover decoding, and an Ogg Vorbis decode
// benchmark. Build/flash with `pio run -e feasibility -t upload`.
// Commands (newline-terminated, over the TinyUSB CDC serial port):
//   MSC 1 | MSC 0         export SD card over USB / take it back
//   LS <dir>              list a directory
//   PLAY <path>           play; logs CPU/heap/gaps every 2 s
//   SEEK <seconds>        jump within the playing file
//   STOP
//   COVER <path>          decode embedded (m4a covr) or plain JPEG, write BMP
//   VORBIS <path> <sec>   decode <sec> seconds of Ogg Vorbis as fast as possible
//   HEAP

#include <Arduino.h>
#include <Audio.h>
#include <JPEGDEC.h>
#include <SD_MMC.h>
#include <USB.h>
#include <USBCDC.h>
#include <USBMSC.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <sdmmc_cmd.h>

#include <atomic>
#include <map>
#include <cstdio>
#include <string>
#include <vector>

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MALLOC(sz) trackedMalloc(sz)
#define STBI_REALLOC(p, sz) trackedRealloc(p, sz)
#define STBI_FREE(p) trackedFree(p)
static void *trackedMalloc(size_t sz);
static void *trackedRealloc(void *p, size_t sz);
static void trackedFree(void *p);
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

USBCDC USBSerial;
USBMSC MSC;
#define LOG USBSerial

// ---- pins (lib/drivers-sd/SdInit.h, lib/drivers-audio) ----
constexpr int kSdCmdPin = 3, kSdClkPin = 4, kSdD0Pin = 5, kSdD1Pin = 6,
              kSdD2Pin = 42, kSdD3Pin = 2;
constexpr uint8_t kBclk = 39, kLrc = 40, kDout = 41;

static bool mountSd() {
  for (int p : {kSdCmdPin, kSdD0Pin, kSdD1Pin, kSdD2Pin, kSdD3Pin})
    pinMode(p, INPUT_PULLUP);
  SD_MMC.setPins(kSdClkPin, kSdCmdPin, kSdD0Pin, kSdD1Pin, kSdD2Pin, kSdD3Pin);
  return SD_MMC.begin("/sdcard", false, false, SDMMC_FREQ_HIGHSPEED);
}

struct CardAccess : fs::SDMMCFS {
  static sdmmc_card_t *get() { return static_cast<CardAccess &>(SD_MMC)._card; }
};

// ---- tracked allocations for stb_image peak ----
struct AllocHdr { size_t size; };
static size_t g_allocCur = 0, g_allocPeak = 0;
static void *trackedMalloc(size_t sz) {
  auto *h = static_cast<AllocHdr *>(heap_caps_malloc(sz + sizeof(AllocHdr), MALLOC_CAP_SPIRAM));
  if (!h) return nullptr;
  h->size = sz;
  g_allocCur += sz;
  if (g_allocCur > g_allocPeak) g_allocPeak = g_allocCur;
  return h + 1;
}
static void trackedFree(void *p) {
  if (!p) return;
  auto *h = static_cast<AllocHdr *>(p) - 1;
  g_allocCur -= h->size;
  heap_caps_free(h);
}
static void *trackedRealloc(void *p, size_t sz) {
  void *n = trackedMalloc(sz);
  if (p && n) {
    size_t old = (static_cast<AllocHdr *>(p) - 1)->size;
    memcpy(n, p, old < sz ? old : sz);
  }
  trackedFree(p);
  return n;
}

// ---- CPU load on core 0 via a low-priority counter ----
static std::atomic<uint64_t> g_idleCount{0};
static void counterTask(void *) {
  uint64_t local = 0;
  int64_t lastYield = esp_timer_get_time();
  for (;;) {
    for (int i = 0; i < 1000; ++i) ++local;
    g_idleCount.store(local, std::memory_order_relaxed);
    int64_t now = esp_timer_get_time();
    if (now - lastYield > 50000) {  // let IDLE0 feed the watchdog
      vTaskDelay(1);
      lastYield = esp_timer_get_time();
    }
  }
}
static double g_baselineRate = 0;  // counts per second with nothing running

// ---- audio ----
Audio audio;
SemaphoreHandle_t g_audioMutex;
static std::atomic<bool> g_playing{false};
static std::atomic<int64_t> g_lastSampleUs{0};
static std::atomic<int64_t> g_maxGapUs{0};
static std::atomic<uint32_t> g_gapsOver50ms{0};

void audio_process_i2s(uint32_t *sample, bool *continueI2S) {
  int64_t now = esp_timer_get_time();
  int64_t last = g_lastSampleUs.load(std::memory_order_relaxed);
  if (last) {
    int64_t gap = now - last;
    if (gap > g_maxGapUs.load(std::memory_order_relaxed)) g_maxGapUs.store(gap);
    if (gap > 50000) g_gapsOver50ms++;
  }
  g_lastSampleUs.store(now, std::memory_order_relaxed);
  // Undo the library's >>1 like the app does.
  int16_t l = static_cast<int16_t>(*sample >> 16), r = static_cast<int16_t>(*sample & 0xFFFF);
  l = constrain(l * 2, INT16_MIN, INT16_MAX);
  r = constrain(r * 2, INT16_MIN, INT16_MAX);
  *sample = (uint32_t(uint16_t(l)) << 16) | uint16_t(r);
  *continueI2S = true;
}

void audio_info(const char *info) { LOG.printf("[audio] %s\n", info); }
void audio_eof_mp3(const char *info) { LOG.printf("[audio] EOF %s\n", info); }

static void audioTask(void *) {
  for (;;) {
    xSemaphoreTake(g_audioMutex, portMAX_DELAY);
    audio.loop();
    bool running = audio.isRunning();
    xSemaphoreGive(g_audioMutex);
    vTaskDelay(running ? 1 : 5);  // same as the app's audio task
  }
}

// ---- MSC ----
static uint8_t *g_dmaBuf = nullptr;
static constexpr size_t kDmaBufSize = 32 * 1024;
static std::atomic<uint64_t> g_mscBytesWritten{0}, g_mscBytesRead{0};

// Virtual disk: known sectors + zeros, writes kept in RAM. Still reads the
// real card for identical timing.
static bool g_virt = false;
static uint32_t g_virtSectors = 0;
static std::map<uint32_t, std::vector<uint8_t>> g_virtData;

static int32_t mscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  if (g_virt) {
    sdmmc_read_sectors(CardAccess::get(), g_dmaBuf, lba % 1000000, bufsize / 512);
    memset(buffer, 0, bufsize);
    for (uint32_t i = 0; i < bufsize / 512; ++i) {
      auto it = g_virtData.find(lba + i);
      if (it != g_virtData.end()) memcpy((uint8_t *)buffer + i * 512, it->second.data(), 512);
    }
    g_mscBytesRead += bufsize;
    return bufsize;
  }
  sdmmc_card_t *card = CardAccess::get();
  uint32_t secs = bufsize / 512;
  static uint32_t calls = 0;
  if (calls++ < 12) LOG.printf("[msc] read lba=%u off=%u size=%u\n", lba, offset, bufsize);
  esp_err_t err = sdmmc_read_sectors(card, g_dmaBuf, lba, secs);
  if (err != ESP_OK) { LOG.printf("[msc] READ ERROR lba=%u size=%u err=0x%x\n", lba, bufsize, err); return -1; }
  memcpy(buffer, g_dmaBuf, bufsize);
  g_mscBytesRead += bufsize;
  return bufsize;
}
static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  if (g_virt) {
    for (uint32_t i = 0; i < bufsize / 512; ++i)
      g_virtData[lba + i].assign(buffer + i * 512, buffer + (i + 1) * 512);
    g_mscBytesWritten += bufsize;
    return bufsize;
  }
  sdmmc_card_t *card = CardAccess::get();
  memcpy(g_dmaBuf, buffer, bufsize);
  LOG.printf("[msc] write lba=%u size=%u\n", lba, bufsize);
  esp_err_t err = sdmmc_write_sectors(card, g_dmaBuf, lba, bufsize / 512);
  if (err != ESP_OK) { LOG.printf("[msc] WRITE ERROR lba=%u err=0x%x\n", lba, err); return -1; }
  g_mscBytesWritten += bufsize;
  return bufsize;
}
static std::atomic<bool> g_ejectRequested{false};
static bool mscStartStop(uint8_t power, bool start, bool loadEject) {
  LOG.printf("[msc] start_stop power=%u start=%d eject=%d\n", power, start, loadEject);
  if (loadEject && !start) g_ejectRequested = true;
  return true;
}
static bool g_mscOn = false;

static void setMsc(bool on) {
  if (on == g_mscOn) return;
  if (on) {
    xSemaphoreTake(g_audioMutex, portMAX_DELAY);
    audio.stopSong();
    xSemaphoreGive(g_audioMutex);
    g_playing = false;
    sdmmc_card_t *card = CardAccess::get();
    if (!card) { LOG.println("[msc] no card"); return; }
    uint32_t sectors = g_virt ? g_virtSectors : card->csd.capacity;
    LOG.printf("[msc] exporting %u sectors (%.1f GB), sector size %d\n", sectors,
               sectors * 512.0 / 1e9, card->csd.sector_size);
    MSC.mediaPresent(true);
    MSC.begin(sectors, 512);
    g_mscOn = true;
  } else {
    MSC.mediaPresent(false);
    g_mscOn = false;
    // Remount so FATFS sees what the host wrote.
    SD_MMC.end();
    LOG.printf("[msc] remount %s\n", mountSd() ? "OK" : "FAILED");
  }
}

// ---- helpers ----
static void printHeap(const char *tag) {
  LOG.printf("[heap %s] internal free=%u min=%u largest=%u | psram free=%u\n", tag,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static uint32_t be32(const uint8_t *b) { return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }

// Finds moov/udta/meta/ilst/covr/data payload in an MP4 file.
static bool findM4aCover(FILE *f, long *outOffset, uint32_t *outLen) {
  fseek(f, 0, SEEK_END);
  long fileEnd = ftell(f);
  const char *path[] = {"moov", "udta", "meta", "ilst", "covr", "data"};
  long start = 0, end = fileEnd;
  for (int depth = 0; depth < 6; ++depth) {
    long pos = start;
    bool found = false;
    while (pos + 8 <= end) {
      uint8_t hdr[16];
      fseek(f, pos, SEEK_SET);
      if (fread(hdr, 1, 8, f) != 8) return false;
      uint64_t size = be32(hdr);
      long hdrLen = 8;
      if (size == 1) {
        if (fread(hdr + 8, 1, 8, f) != 8) return false;
        size = (uint64_t(be32(hdr + 8)) << 32) | be32(hdr + 12);
        hdrLen = 16;
      } else if (size == 0) {
        size = end - pos;
      }
      if (size < 8) return false;
      if (memcmp(hdr + 4, path[depth], 4) == 0) {
        start = pos + hdrLen;
        end = pos + size;
        if (depth == 2) start += 4;  // meta is a full box
        if (depth == 5) start += 8;  // data: type + locale
        found = true;
        break;
      }
      pos += size;
    }
    if (!found) {
      LOG.printf("[cover] atom %s not found\n", path[depth]);
      return false;
    }
  }
  *outOffset = start;
  *outLen = end - start;
  return true;
}

static void writeBmp(const std::string &path, const uint8_t *rgb, int w, int h) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) { LOG.printf("[cover] cannot write %s\n", path.c_str()); return; }
  int rowPad = (4 - (w * 3) % 4) % 4;
  uint32_t dataSize = (w * 3 + rowPad) * h;
  uint8_t hdr[54] = {'B', 'M'};
  auto le32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; ++i) hdr[off + i] = v >> (8 * i); };
  le32(2, 54 + dataSize); le32(10, 54); le32(14, 40); le32(18, w); le32(22, h);
  hdr[26] = 1; hdr[28] = 24; le32(34, dataSize);
  fwrite(hdr, 1, 54, f);
  uint8_t pad[3] = {0};
  for (int y = h - 1; y >= 0; --y) {
    for (int x = 0; x < w; ++x) {
      const uint8_t *p = rgb + (y * w + x) * 3;
      uint8_t bgr[3] = {p[2], p[1], p[0]};
      fwrite(bgr, 1, 3, f);
    }
    fwrite(pad, 1, rowPad, f);
  }
  fclose(f);
}

static JPEGDEC g_jpegdec;
static std::vector<uint16_t> g_dcPixels;
static int g_dcW = 0, g_dcH = 0;
static int dcDraw(JPEGDRAW *d) {
  for (int y = 0; y < d->iHeight; ++y)
    for (int x = 0; x < d->iWidth; ++x) {
      int dx = d->x + x, dy = d->y + y;
      if (dx < g_dcW && dy < g_dcH) g_dcPixels[dy * g_dcW + dx] = d->pPixels[y * d->iWidth + x];
    }
  return 1;
}
static void cmdCover2(const std::string &sdPath, const std::vector<uint8_t> &jpeg) {
  int64_t t0 = esp_timer_get_time();
  uint32_t psBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  if (!g_jpegdec.openRAM((uint8_t *)jpeg.data(), jpeg.size(), dcDraw)) { LOG.println("[jpegdec] open failed"); return; }
  int type = g_jpegdec.getJPEGType();
  int w = g_jpegdec.getWidth(), h = g_jpegdec.getHeight();
  int scale = type == JPEG_MODE_PROGRESSIVE ? JPEG_SCALE_EIGHTH : 0;
  int div = scale ? 8 : 1;
  g_dcW = (w + div - 1) / div; g_dcH = (h + div - 1) / div;
  g_dcPixels.assign(g_dcW * g_dcH, 0);
  int ok = g_jpegdec.decode(0, 0, scale);
  g_jpegdec.close();
  int64_t t1 = esp_timer_get_time();
  LOG.printf("[jpegdec] %ux%u %s -> %dx%d ok=%d in %lld ms, psram used ~%d KB (incl. out buf)\n", w, h,
             type == JPEG_MODE_PROGRESSIVE ? "progressive(DC-only 1/8)" : "baseline", g_dcW, g_dcH, ok,
             (t1 - t0) / 1000, (int)(psBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024);
  // Write the DC image as-is (RGB888 BMP) so it can be inspected.
  std::vector<uint8_t> rgb(g_dcW * g_dcH * 3);
  for (int i = 0; i < g_dcW * g_dcH; ++i) {
    uint16_t p = g_dcPixels[i];
    rgb[i * 3] = (p >> 11) * 255 / 31; rgb[i * 3 + 1] = ((p >> 5) & 63) * 255 / 63; rgb[i * 3 + 2] = (p & 31) * 255 / 31;
  }
  auto slash = sdPath.find_last_of('/');
  mkdir("/sdcard/feas/out", 0777);
  writeBmp("/sdcard/feas/out/" + sdPath.substr(slash + 1) + ".dc.bmp", rgb.data(), g_dcW, g_dcH);
}

static void cmdCover(const std::string &sdPath) {
  std::string path = "/sdcard" + sdPath;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f) { LOG.printf("[cover] open failed %s\n", path.c_str()); return; }
  int64_t t0 = esp_timer_get_time();
  long offset = 0;
  uint32_t len = 0;
  std::string lower = sdPath;
  for (auto &c : lower) c = tolower(c);
  if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".m4a") {
    if (!findM4aCover(f, &offset, &len)) { fclose(f); return; }
  } else {
    fseek(f, 0, SEEK_END);
    len = ftell(f);
  }
  std::vector<uint8_t, std::allocator<uint8_t>> jpeg(len);
  fseek(f, offset, SEEK_SET);
  size_t got = fread(jpeg.data(), 1, len, f);
  fclose(f);
  int64_t t1 = esp_timer_get_time();
  bool progressive = false;
  for (uint32_t i = 0; i + 1 < len; ++i)
    if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC2) { progressive = true; break; }
    else if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC0) break;
  cmdCover2(sdPath, jpeg);
  printHeap("before decode");
  g_allocPeak = g_allocCur = 0;
  int w, h, n;
  uint8_t *rgb = stbi_load_from_memory(jpeg.data(), got, &w, &h, &n, 3);
  int64_t t2 = esp_timer_get_time();
  if (!rgb) { LOG.printf("[cover] stbi failed: %s\n", stbi_failure_reason()); return; }
  // Center crop + area-average downscale to 96x96.
  constexpr int kOut = 96;
  int crop = std::min(w, h), cx = (w - crop) / 2, cy = (h - crop) / 2;
  std::vector<uint8_t> out(kOut * kOut * 3);
  for (int oy = 0; oy < kOut; ++oy)
    for (int ox = 0; ox < kOut; ++ox) {
      int x0 = cx + ox * crop / kOut, x1 = cx + (ox + 1) * crop / kOut;
      int y0 = cy + oy * crop / kOut, y1 = cy + (oy + 1) * crop / kOut;
      uint32_t s[3] = {0}, cnt = 0;
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
          const uint8_t *p = rgb + (y * w + x) * 3;
          s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; ++cnt;
        }
      for (int c = 0; c < 3; ++c) out[(oy * kOut + ox) * 3 + c] = cnt ? s[c] / cnt : 0;
    }
  int64_t t3 = esp_timer_get_time();
  stbi_image_free(rgb);
  mkdir("/sdcard/feas/out", 0777);
  auto slash = sdPath.find_last_of('/');
  std::string name = sdPath.substr(slash + 1);
  writeBmp("/sdcard/feas/out/" + name + ".bmp", out.data(), kOut, kOut);
  LOG.printf("[cover] %s: %ux%u %s jpeg=%u B | read %lld ms, decode %lld ms, "
             "scale %lld ms | stb peak alloc %u KB\n",
             name.c_str(), w, h, progressive ? "PROGRESSIVE" : "baseline", len,
             (t1 - t0) / 1000, (t2 - t1) / 1000, (t3 - t2) / 1000, g_allocPeak / 1024);
  printHeap("after decode");
}

static void cmdVorbis(const std::string &sdPath, int seconds) {
  std::string path = "/sdcard" + sdPath;
  printHeap("before vorbis");
  int err = 0;
  int64_t t0 = esp_timer_get_time();
  stb_vorbis *v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
  if (!v) { LOG.printf("[vorbis] open failed err=%d\n", err); return; }
  stb_vorbis_info info = stb_vorbis_get_info(v);
  int64_t t1 = esp_timer_get_time();
  LOG.printf("[vorbis] %u Hz, %d ch, setup mem %u, open %lld ms, total %.1f s\n",
             info.sample_rate, info.channels, info.setup_memory_required,
             (t1 - t0) / 1000, stb_vorbis_stream_length_in_seconds(v));
  printHeap("vorbis open");
  static short pcm[4096];
  uint64_t frames = 0, target = uint64_t(seconds) * info.sample_rate;
  while (frames < target) {
    int n = stb_vorbis_get_samples_short_interleaved(v, info.channels, pcm, sizeof(pcm) / sizeof(short));
    if (n <= 0) break;
    frames += n;
    if ((frames & 0x3FFF) < (uint64_t)n) vTaskDelay(1);
  }
  int64_t t2 = esp_timer_get_time();
  double audioSec = double(frames) / info.sample_rate;
  double wallSec = (t2 - t1) / 1e6;
  LOG.printf("[vorbis] decoded %.1f s audio in %.2f s wall -> %.1fx realtime "
             "(single core, one core-share of %.0f%%)\n",
             audioSec, wallSec, audioSec / wallSec, 100.0 * wallSec / audioSec);
  // Seek test.
  int64_t t3 = esp_timer_get_time();
  int ok = stb_vorbis_seek(v, info.sample_rate * 600);
  LOG.printf("[vorbis] seek to 10:00 %s in %lld ms\n", ok ? "OK" : "FAILED",
             (esp_timer_get_time() - t3) / 1000);
  stb_vorbis_close(v);
  printHeap("after vorbis");
}

static void cmdLs(const std::string &dir) {
  File d = SD_MMC.open(dir.c_str());
  if (!d) { LOG.println("[ls] open failed"); return; }
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    LOG.printf("  %s%s %u\n", e.name(), e.isDirectory() ? "/" : "", (unsigned)e.size());
    e.close();
  }
  d.close();
}

static void measureBaseline() {
  uint64_t c0 = g_idleCount.load();
  delay(2000);
  g_baselineRate = (g_idleCount.load() - c0) / 2.0;
  LOG.printf("[cpu] baseline counter rate %.0f/s\n", g_baselineRate);
}

static uint64_t g_lastCount = 0;
static uint32_t g_lastReport = 0;
static uint64_t g_lastMscBytes = 0;

void setup() {
  heap_caps_malloc_extmem_enable(32);  // like the app
  g_dmaBuf = static_cast<uint8_t *>(heap_caps_malloc(kDmaBufSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  MSC.vendorID("knobify");
  MSC.productID("SD card");
  MSC.productRevision("1.0");
  MSC.onRead(mscRead);
  MSC.onWrite(mscWrite);
  MSC.onStartStop(mscStartStop);
  MSC.mediaPresent(false);
  USBSerial.setRxBufferSize(32768);
  USBSerial.begin();
  USB.begin();
  delay(1500);
  LOG.println("\n[feas] boot");
  LOG.printf("[sd] mount %s\n", mountSd() ? "OK" : "FAILED");
  printHeap("boot");
  g_audioMutex = xSemaphoreCreateMutex();
  audio.setPinout(kBclk, kLrc, kDout);
  audio.setBufsize(-1, 64 * 1024);
  audio.setVolume(12);
  xTaskCreatePinnedToCore(counterTask, "counter", 2048, nullptr, 1, nullptr, 0);
  delay(200);
  measureBaseline();
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, nullptr, 0);
  g_lastCount = g_idleCount.load();
  g_lastReport = millis();
  LOG.println("[feas] ready");
}

void loop() {
  static std::string line;
  while (LOG.available()) {
    char c = LOG.read();
    if (c == '\r') continue;
    if (c != '\n') { line += c; continue; }
    std::string cmd = line, arg;
    line.clear();
    auto sp = cmd.find(' ');
    if (sp != std::string::npos) { arg = cmd.substr(sp + 1); cmd = cmd.substr(0, sp); }
    LOG.printf("> %s %s\n", cmd.c_str(), arg.c_str());
    if (cmd == "MSC") setMsc(arg == "1");
    else if (g_mscOn && cmd != "HEAP" && cmd != "BPB") LOG.println("[feas] MSC active, send MSC 0 first");
    else if (cmd == "LS") cmdLs(arg.empty() ? "/" : arg);
    else if (cmd == "PLAY") {
      g_maxGapUs = 0; g_gapsOver50ms = 0; g_lastSampleUs = 0;
      printHeap("before play");
      xSemaphoreTake(g_audioMutex, portMAX_DELAY);
      int64_t t0 = esp_timer_get_time();
      bool ok = audio.connecttoFS(SD_MMC, arg.c_str());
      int64_t t1 = esp_timer_get_time();
      xSemaphoreGive(g_audioMutex);
      LOG.printf("[play] connecttoFS %s in %lld ms\n", ok ? "OK" : "FAILED", (t1 - t0) / 1000);
      g_playing = ok;
    } else if (cmd == "SEEK") {
      xSemaphoreTake(g_audioMutex, portMAX_DELAY);
      g_lastSampleUs = 0;
      bool ok = audio.setAudioPlayPosition(atoi(arg.c_str()));
      xSemaphoreGive(g_audioMutex);
      LOG.printf("[play] seek %s\n", ok ? "OK" : "FAILED");
    } else if (cmd == "SEEKB") {
      xSemaphoreTake(g_audioMutex, portMAX_DELAY);
      g_lastSampleUs = 0;
      bool ok = audio.setFilePos(strtoul(arg.c_str(), nullptr, 10));
      xSemaphoreGive(g_audioMutex);
      LOG.printf("[play] seek byte %s\n", ok ? "OK" : "FAILED");
    } else if (cmd == "STOP") {
      xSemaphoreTake(g_audioMutex, portMAX_DELAY);
      audio.stopSong();
      xSemaphoreGive(g_audioMutex);
      g_playing = false;
    } else if (cmd == "COVER") cmdCover(arg);
    else if (cmd == "VORBIS") {
      auto sp2 = arg.find_last_of(' ');
      struct Job { std::string path; int secs; SemaphoreHandle_t done; };
      static Job job;
      job = {arg.substr(0, sp2), atoi(arg.substr(sp2 + 1).c_str()), xSemaphoreCreateBinary()};
      xTaskCreatePinnedToCore([](void *p) {
        auto *j = static_cast<Job *>(p);
        cmdVorbis(j->path, j->secs);
        xSemaphoreGive(j->done);
        vTaskDelete(nullptr);
      }, "vorbis", 32768, &job, 3, nullptr, 0);
      xSemaphoreTake(job.done, portMAX_DELAY);
      vSemaphoreDelete(job.done);
    } else if (cmd == "HEAP") printHeap("now");
    else if (cmd == "PUT") {  // PUT <size> <path>, then <size> raw bytes
      auto sp2 = arg.find(' ');
      size_t size = strtoul(arg.substr(0, sp2).c_str(), nullptr, 10);
      std::string path = "/sdcard" + arg.substr(sp2 + 1);
      for (size_t i = 8; i < path.size(); ++i)
        if (path[i] == '/') { mkdir(path.substr(0, i).c_str(), 0777); }
      FILE *f = fopen(path.c_str(), "wb");
      LOG.printf("[put] READY %s\n", f ? "OK" : "FAIL");
      if (!f) continue;
      static uint8_t *buf = static_cast<uint8_t *>(heap_caps_malloc(16384, MALLOC_CAP_SPIRAM));
      size_t got = 0, chunkGot = 0; uint32_t sum = 0;
      constexpr size_t kChunk = 8192;
      int64_t t0 = esp_timer_get_time(), lastData = t0;
      while (got < size && esp_timer_get_time() - lastData < 3000000) {
        int n = LOG.read(buf, std::min<size_t>(kChunk - chunkGot, size - got));
        if (n <= 0) { delay(1); continue; }
        lastData = esp_timer_get_time();
        for (int i = 0; i < n; ++i) sum += buf[i];
        fwrite(buf, 1, n, f);
        got += n; chunkGot += n;
        if (chunkGot == kChunk || got == size) { LOG.write('#'); LOG.flush(); chunkGot = 0; }
      }
      fclose(f);
      double secs = (esp_timer_get_time() - t0) / 1e6;
      LOG.printf("[put] DONE %u/%u bytes sum=%u %.2f MB/s\n", got, size, sum, got / secs / 1e6);
    }
    else if (cmd == "VIRT") {  // VIRT <sectorfile> <totalSectors>
      auto sp2 = arg.find(' ');
      FILE *f = fopen(("/sdcard" + arg.substr(0, sp2)).c_str(), "r");
      if (!f) { LOG.println("[virt] open failed"); continue; }
      g_virtData.clear();
      static char lineBuf[1100];
      while (fgets(lineBuf, sizeof(lineBuf), f)) {
        char *hex = strchr(lineBuf, ' ');
        if (!hex) continue;
        uint32_t lba = strtoul(lineBuf, nullptr, 10);
        std::vector<uint8_t> sec(512);
        for (int i = 0; i < 512; ++i) { char h[3] = {hex[1 + 2 * i], hex[2 + 2 * i], 0}; sec[i] = strtoul(h, nullptr, 16); }
        g_virtData[lba] = sec;
      }
      fclose(f);
      g_virtSectors = strtoul(arg.substr(sp2 + 1).c_str(), nullptr, 10);
      g_virt = true;
      g_mscBytesRead = 0;
      LOG.printf("[virt] %u known sectors, %u total\n", (unsigned)g_virtData.size(), g_virtSectors);
    }
    else if (cmd == "GET") {
      FILE *f = fopen(("/sdcard" + arg).c_str(), "rb");
      if (!f) { LOG.println("[get] FAIL"); continue; }
      fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
      LOG.printf("[get] SIZE %ld\n", n);
      uint8_t b[1024]; size_t r;
      while ((r = fread(b, 1, sizeof(b), f)) > 0) LOG.write(b, r);
      fclose(f); LOG.flush();
    }
    else if (cmd == "BPB") {
      sdmmc_card_t *card = CardAccess::get();
      sdmmc_read_sectors(card, g_dmaBuf, 0, 1);
      uint32_t start = g_dmaBuf[454] | (g_dmaBuf[455] << 8) | (g_dmaBuf[456] << 16) | (g_dmaBuf[457] << 24);
      sdmmc_read_sectors(card, g_dmaBuf, start, 1);
      const uint8_t *b = g_dmaBuf;
      uint16_t bps = b[11] | (b[12] << 8);
      uint8_t spc = b[13];
      uint16_t rsvd = b[14] | (b[15] << 8);
      uint8_t nfats = b[16];
      uint32_t fatsz = b[36] | (b[37] << 8) | (b[38] << 16) | (b[39] << 24);
      uint32_t tot = b[32] | (b[33] << 8) | (b[34] << 16) | (b[35] << 24);
      LOG.printf("[bpb] part start %u, bytes/sec %u, sec/clus %u (cluster %u KB), reserved %u, fats %u, "
                 "fat size %u sectors (%.1f MB each), total sectors %u, fsinfo dirty-flags byte %02x\n",
                 start, bps, spc, bps * spc / 1024, rsvd, nfats, fatsz, fatsz * bps / 1e6, tot, b[65]);
      sdmmc_read_sectors(card, g_dmaBuf, start + rsvd, 1);
      uint32_t fat1 = g_dmaBuf[4] | (g_dmaBuf[5] << 8) | (g_dmaBuf[6] << 16) | (g_dmaBuf[7] << 24);
      LOG.printf("[bpb] FAT[1]=%08x clean-shutdown=%d no-io-error=%d\n", fat1, !!(fat1 & 0x08000000), !!(fat1 & 0x04000000));
    }
    else LOG.println("[feas] unknown command");
  }

  if (g_ejectRequested.exchange(false)) {
    LOG.println("[msc] host ejected -> remount");
    setMsc(false);
  }

  uint32_t now = millis();
  if (now - g_lastReport >= 2000) {
    uint64_t count = g_idleCount.load();
    double rate = (count - g_lastCount) * 1000.0 / (now - g_lastReport);
    g_lastCount = count;
    g_lastReport = now;
    if (g_playing) {
      xSemaphoreTake(g_audioMutex, portMAX_DELAY);
      bool running = audio.isRunning();
      uint32_t pos = audio.getAudioCurrentTime(), dur = audio.getAudioFileDuration();
      uint32_t sr = audio.getSampleRate(), br = audio.getBitRate();
      xSemaphoreGive(g_audioMutex);
      LOG.printf("[play] %s %u/%u s sr=%u br=%u | core0 busy %.0f%% | max gap %lld ms, "
                 "gaps>50ms %u | internal free %u\n",
                 running ? "RUN" : "STOPPED", pos, dur, sr, br,
                 100.0 * (1.0 - rate / g_baselineRate), g_maxGapUs.load() / 1000,
                 g_gapsOver50ms.load(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      g_maxGapUs = 0;
      if (!running) g_playing = false;
    }
    if (g_mscOn) {
      uint64_t b = g_mscBytesWritten.load() + g_mscBytesRead.load();
      if (b != g_lastMscBytes) {
        LOG.printf("[msc] %.2f MB/s (read total %.1f MB, written total %.1f MB)\n",
                   (b - g_lastMscBytes) / 2.0 / 1e6, g_mscBytesRead.load() / 1e6, g_mscBytesWritten.load() / 1e6);
        g_lastMscBytes = b;
      }
    }
  }
  delay(2);
}
