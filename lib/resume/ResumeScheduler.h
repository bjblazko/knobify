#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "BlobStore.h"
#include "ResumeCodec.h"
#include "ResumeSource.h"

namespace knobify::resume {

// Restores the saved ResumeRecord at boot and keeps it up to date while
// running (ADR 0012). There is no shutdown signal, so it saves periodically,
// deciding by value what changed rather than being told:
//
// - anything but the playback position (screen, tab, track, shuffle,
//   play/pause): saved once it has been stable for kStructureDelayMs;
// - only the position (a track playing on): at most every kPositionIntervalMs;
// - nothing changed (paused, idle): no writes at all.
class ResumeScheduler {
 public:
  static constexpr char kKey[] = "resume";
  static constexpr uint32_t kCaptureIntervalMs = 1000;
  static constexpr uint32_t kStructureDelayMs = 3000;
  static constexpr uint32_t kPositionIntervalMs = 30000;

  // Sources restore in this order -- put ones others depend on first (music
  // before navigation, which drops Now Playing without a queue).
  ResumeScheduler(BlobStore &store, std::vector<ResumeSource *> sources)
      : store_(store), sources_(std::move(sources)) {}

  // Call once at boot, after everything the sources resolve against (the
  // library index) is loaded. Returns false if nothing valid was saved.
  bool restore(uint32_t nowMs) {
    std::vector<uint8_t> bytes;
    ResumeRecord record;
    if (!store_.getBlob(kKey, bytes) || !ResumeCodec::decode(bytes, record)) {
      begin(ResumeRecord{}, nowMs);
      return false;
    }
    for (ResumeSource *source : sources_) source->restore(record, nowMs);
    begin(record, nowMs);
    return true;
  }

  // Deletes the saved record, e.g. after a crash that it might have caused.
  void discard(uint32_t nowMs) {
    store_.removeBlob(kKey);
    begin(ResumeRecord{}, nowMs);
  }

  // Call every loop(); cheap between capture intervals.
  void tick(uint32_t nowMs) {
    if (nowMs - lastCaptureMs_ < kCaptureIntervalMs) return;
    lastCaptureMs_ = nowMs;

    ResumeRecord record;
    for (ResumeSource *source : sources_) source->capture(record, nowMs);
    ResumeRecord structure = withoutPosition(record);
    if (structure != lastStructure_) {
      lastStructure_ = structure;
      structureChangedMs_ = nowMs;
    }
    if (record == saved_) return;

    bool due = structure != withoutPosition(saved_)
                   ? nowMs - structureChangedMs_ >= kStructureDelayMs
                   : nowMs - lastSaveMs_ >= kPositionIntervalMs;
    if (!due) return;
    // Remembered even if the write fails, so a failing store is retried on
    // the next change, not hammered every second.
    std::vector<uint8_t> bytes = ResumeCodec::encode(record);
    if (!bytes.empty()) store_.setBlob(kKey, bytes);
    saved_ = std::move(record);
    lastSaveMs_ = nowMs;
    ++saveCount_;
  }

  uint32_t saveCount() const { return saveCount_; }

 private:
  void begin(ResumeRecord saved, uint32_t nowMs) {
    lastStructure_ = withoutPosition(saved);
    saved_ = std::move(saved);
    lastSaveMs_ = nowMs;
    structureChangedMs_ = nowMs;
    lastCaptureMs_ = nowMs - kCaptureIntervalMs;
  }

  static ResumeRecord withoutPosition(ResumeRecord record) {
    if (record.music) {
      record.music->filePosition = 0;
      record.music->elapsedSeconds = 0;
    }
    return record;
  }

  BlobStore &store_;
  std::vector<ResumeSource *> sources_;
  ResumeRecord saved_;
  ResumeRecord lastStructure_;
  uint32_t lastSaveMs_ = 0;
  uint32_t structureChangedMs_ = 0;
  uint32_t lastCaptureMs_ = 0;
  uint32_t saveCount_ = 0;
};

}  // namespace knobify::resume
