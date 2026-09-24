#include "VorbisBackend.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>

#include "AudioOutputStage.h"

// STB_VORBIS_HEADER_ONLY makes this #include produce declarations only
// (no decoder body), so this translation unit can call stb_vorbis_*
// functions without redefining them. The actual implementation comes
// from PlatformIO separately compiling the vendored lib/third_party
// stb_vorbis.c (without this define) as its own translation unit, and
// the two get linked together. If that vendored .c file is ever
// excluded from the build, or this define is dropped so this #include
// pulls in the implementation too, the link fails: either with missing
// symbols (excluded) or duplicate-definition errors (define dropped).
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace knobify::drivers {

bool VorbisBackend::open(const std::string &path, uint32_t startSample) {
  close();
  // The SD card is mounted at /sdcard; stb_vorbis reads it through the
  // normal filesystem rather than knobify's RawFile abstraction, which
  // exists for tag parsing on the host.
  const std::string fsPath = "/sdcard" + path;
  int error = 0;
  stream_ = stb_vorbis_open_filename(fsPath.c_str(), &error, nullptr);
  if (!stream_) {
    Serial.printf("[vorbis] open failed (%d): %s\n", error, path.c_str());
    return false;
  }
  const stb_vorbis_info info = stb_vorbis_get_info(stream_);
  // i2s_set_sample_rates() below rejects 0, as would the library's own
  // Audio::setSampleRate() it replaces -- fuse it the same way.
  sampleRate_ = info.sample_rate ? info.sample_rate : 16000;
  durationSeconds_ =
      static_cast<uint32_t>(stb_vorbis_stream_length_in_seconds(stream_) + 0.5f);
  frames_ = static_cast<int16_t *>(
      heap_caps_malloc(kFramesPerChunk * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (!frames_) {
    Serial.println("[vorbis] out of memory for the frame buffer");
    close();
    return false;
  }
  if (!ensureTask()) {
    close();
    return false;
  }
  // Applied by the decode task before its first chunk: stb_vorbis_seek()
  // needs the task's stack, and open() runs on the main loop's. Exact here:
  // resuming a track (ADR 0012) happens once and should land where the
  // listener stopped, not up to 23 ms earlier.
  startSample_ = startSample;
  currentSample_.store(startSample, std::memory_order_relaxed);
  seekRequest_.store(kNoSeek, std::memory_order_relaxed);
  stopRequested_.store(false, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_relaxed);
  // Program the port's clock before the session starts: reprogramming it
  // once the task may already be blocked in i2s_write() stops and restarts
  // the channel out from under that writer. Audio::setSampleRate() does the
  // equivalent i2s_set_sample_rates() call but is private in this library
  // version.
  if (i2s_set_sample_rates(I2S_NUM_0, sampleRate_) != ESP_OK) {
    Serial.println("[vorbis] could not program the I2S sample rate");
    close();
    return false;
  }
  // Release pairs with the acquire in close(): everything above is visible
  // to the task before it sees a session.
  running_.store(true, std::memory_order_release);
  xTaskNotifyGive(task_);
  return true;
}

bool VorbisBackend::ensureTask() {
  if (task_) return true;
  // Pinned to core 0 at priority 3, like the library's audio task, so it
  // never competes with LVGL on core 1.
  if (xTaskCreatePinnedToCore(&VorbisBackend::taskTrampoline, "vorbis",
                              kTaskStackBytes, this, /*priority=*/3, &task_,
                              /*core=*/0) != pdPASS) {
    task_ = nullptr;
    Serial.printf("[vorbis] could not start the decode task (internal free=%u largest=%u)\n",
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    return false;
  }
  return true;
}

void VorbisBackend::close() {
  // A session in progress checks the flag between chunks (at most ~23 ms of
  // audio) and parks itself; wait for it before freeing anything. Acquire
  // pairs with the release store in runSession().
  if (running_.load(std::memory_order_acquire)) {
    stopRequested_.store(true, std::memory_order_relaxed);
    while (running_.load(std::memory_order_acquire)) delay(2);
  }
  if (stream_) {
    stb_vorbis_close(stream_);
    stream_ = nullptr;
  }
  if (frames_) {
    heap_caps_free(frames_);
    frames_ = nullptr;
  }
  sampleRate_ = 0;
  durationSeconds_ = 0;
  currentSample_.store(0, std::memory_order_relaxed);
}

bool VorbisBackend::seekToSample(uint32_t sample) {
  if (!stream_) return false;
  // Applied by the decode task between chunks: stb_vorbis is not safe to
  // call from two tasks at once.
  seekRequest_.store(sample, std::memory_order_relaxed);
  return true;
}

void VorbisBackend::taskTrampoline(void *self) {
  static_cast<VorbisBackend *>(self)->decodeTask();
}

void VorbisBackend::decodeTask() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    runSession();
  }
}

void VorbisBackend::runSession() {
  auto &stage = audioOutputStage();
  if (startSample_ != 0) stb_vorbis_seek(stream_, startSample_);
  while (!stopRequested_.load(std::memory_order_relaxed)) {
    const uint32_t seekTo = seekRequest_.exchange(kNoSeek, std::memory_order_relaxed);
    // seek_frame, not seek: the exact-sample version decodes and discards
    // audio until it lands on the requested sample, which during a shuttle
    // cue (a seek every ~300 ms, ADR 0013) costs more than the audio
    // between two seeks -- on the device that made winding nearly silent.
    // A frame boundary is at most 1024 samples (23 ms) away, which nobody
    // hears while cueing.
    if (seekTo != kNoSeek && stb_vorbis_seek_frame(stream_, seekTo)) {
      currentSample_.store(seekTo, std::memory_order_relaxed);
    }
    if (paused_.load(std::memory_order_relaxed)) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    const int frames = stb_vorbis_get_samples_short_interleaved(
        stream_, 2, frames_, static_cast<int>(kFramesPerChunk * 2));
    if (frames <= 0) break;  // End of stream.
    // A mono file decodes to two identical channels when 2 is requested,
    // so the output stage always gets interleaved stereo.
    if (!stage.writeFrames(frames_, static_cast<size_t>(frames))) break;
    currentSample_.fetch_add(static_cast<uint32_t>(frames), std::memory_order_relaxed);
  }
  // Otherwise the last DMA buffer keeps looping (an audible buzz/tail)
  // instead of going silent, like the library's own pauseResume()/
  // stopSong() do with the same call.
  i2s_zero_dma_buffer(I2S_NUM_0);
  // Release pairs with the acquire load in close()'s wait loop: close()
  // frees stream_/frames_ right after observing this false, so that free
  // must not be reordered ahead of the work above.
  // The task never touches stream_/frames_ again until the next session.
  running_.store(false, std::memory_order_release);
}

}  // namespace knobify::drivers
