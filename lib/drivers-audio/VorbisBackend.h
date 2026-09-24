#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "DecoderBackend.h"

struct stb_vorbis;

namespace knobify::drivers {

// Ogg Vorbis playback on knobify's own decode path (ADR 0017): stb_vorbis
// reading through the filesystem, decoding on a task of its own because it
// needs about 32 KB of stack -- far more than the shared audio task has (or
// the main loop, which crashed seeking a resumed track). The task is created
// on the first Ogg and then parks between tracks: its stack must be one
// contiguous 32 KB block of internal RAM, and re-allocating it per track
// failed after a few auto-advances once the heap had fragmented (2026-09-24).
// Its working set (~180 KB) lands in PSRAM through the allocator setup in
// setup().
class VorbisBackend : public DecoderBackend {
 public:
  static constexpr uint32_t kTaskStackBytes = 32 * 1024;
  static constexpr size_t kFramesPerChunk = 1024;

  ~VorbisBackend() override { close(); }

  bool open(const std::string &path, uint32_t startSample) override;
  void close() override;
  bool seekToSample(uint32_t sample) override;
  uint32_t currentSample() const override {
    return currentSample_.load(std::memory_order_relaxed);
  }
  uint32_t sampleRate() const override { return sampleRate_; }
  uint32_t durationSeconds() const override { return durationSeconds_; }
  bool running() const override { return running_.load(std::memory_order_relaxed); }
  void setPaused(bool paused) override {
    paused_.store(paused, std::memory_order_relaxed);
  }

 private:
  static void taskTrampoline(void *self);
  void decodeTask();
  void runSession();
  bool ensureTask();

  stb_vorbis *stream_ = nullptr;
  TaskHandle_t task_ = nullptr;
  int16_t *frames_ = nullptr;  // kFramesPerChunk * 2, PSRAM.
  uint32_t startSample_ = 0;
  uint32_t sampleRate_ = 0;
  uint32_t durationSeconds_ = 0;
  std::atomic<uint32_t> currentSample_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<uint32_t> seekRequest_{kNoSeek};

  static constexpr uint32_t kNoSeek = UINT32_MAX;
};

}  // namespace knobify::drivers
