#include "VorbisBackend.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "AudioOutputStage.h"

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
  sampleRate_ = info.sample_rate;
  channels_ = info.channels;
  durationSeconds_ =
      static_cast<uint32_t>(stb_vorbis_stream_length_in_seconds(stream_) + 0.5f);
  frames_ = static_cast<int16_t *>(
      heap_caps_malloc(kFramesPerChunk * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM));
  if (!frames_) {
    Serial.println("[vorbis] out of memory for the frame buffer");
    close();
    return false;
  }
  if (startSample != 0) stb_vorbis_seek(stream_, startSample);
  currentSample_.store(startSample, std::memory_order_relaxed);
  stopRequested_.store(false, std::memory_order_relaxed);
  paused_.store(false, std::memory_order_relaxed);
  running_.store(true, std::memory_order_relaxed);
  return true;
}

void VorbisBackend::close() {
  if (task_) {
    stopRequested_.store(true, std::memory_order_relaxed);
    // The decode loop checks the flag between chunks (at most ~23 ms of
    // audio) and deletes itself; wait for it before freeing anything.
    while (running_.load(std::memory_order_relaxed)) delay(2);
    task_ = nullptr;
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
  running_.store(false, std::memory_order_relaxed);
}

bool VorbisBackend::seekToSample(uint32_t sample) {
  if (!stream_) return false;
  // Applied by the decode task between chunks: stb_vorbis is not safe to
  // call from two tasks at once.
  seekRequest_.store(sample, std::memory_order_relaxed);
  return true;
}

}  // namespace knobify::drivers
