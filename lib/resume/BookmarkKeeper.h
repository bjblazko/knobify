#pragma once

#include <cstdint>
#include <string>

#include "BlobStore.h"
#include "Bookmarks.h"
#include "CollectionSet.h"
#include "CoverArtCache.h"
#include "PlaybackStateMachine.h"

namespace knobify::resume {

// Keeps Bookmarks current while a spoken-word title plays, and persists
// them (ADR 0018).
//
// Same shape as ResumeScheduler and for the same reason: there is no
// shutdown signal, so it samples periodically and writes only when
// something actually changed. It writes far less often than the session
// resume does -- a bookmark that is a few seconds stale costs nothing,
// and NVS writes are the thing worth being stingy with.
class BookmarkKeeper {
 public:
  static constexpr uint32_t kObserveIntervalMs = 2000;
  static constexpr uint32_t kSaveIntervalMs = 30000;

  BookmarkKeeper(Bookmarks &marks, BlobStore &store,
                 const collection::CollectionSet &collections,
                 const playback::PlaybackStateMachine &playback)
      : marks_(marks),
        store_(store),
        collections_(collections),
        playback_(playback) {}

  // Loads what was saved. A missing or unreadable blob just means no
  // bookmarks yet.
  void begin() {
    std::vector<uint8_t> bytes;
    if (store_.getBlob(Bookmarks::kKey, bytes)) marks_.decode(bytes);
  }

  // Call every loop(); cheap between intervals.
  void tick(uint32_t nowMs) {
    if (nowMs - lastObserveMs_ >= kObserveIntervalMs) {
      lastObserveMs_ = nowMs;
      observe(nowMs);
    }
    if (nowMs - lastSaveMs_ >= kSaveIntervalMs) {
      lastSaveMs_ = nowMs;
      flush();
    }
  }

  // Before deep sleep, where there is no next tick.
  void saveNow(uint32_t nowMs) {
    observe(nowMs);
    flush();
  }

  // The bookmark key for a track: the folder holding it, which is the
  // title (one audiobook, one radio play) rather than the part.
  static std::string titleKeyFor(const std::string &trackPath) {
    return library::CoverArtCache::albumFolderPathFor(trackPath);
  }

  // Whether a path is in a collection that remembers positions at all.
  bool remembers(const std::string &trackPath) const {
    collection::CollectionId id = collection::CollectionId::Music;
    if (!collections_.findByPath(trackPath, id)) return false;
    return collections_.profile(id).resumesWithinTitle;
  }

 private:
  void observe(uint32_t nowMs) {
    if (!playback_.hasQueue()) return;
    const std::string &path = playback_.currentPath();
    if (path.empty() || !remembers(path)) return;
    Bookmark mark;
    mark.titleKey = titleKeyFor(path);
    mark.trackPath = path;
    mark.filePosition = playback_.filePosition();
    mark.elapsedSeconds = playback_.elapsedMs(nowMs) / 1000;
    if (mark.elapsedSeconds > Bookmarks::kMaxElapsedSeconds) return;
    marks_.note(mark);
  }

  void flush() {
    if (!marks_.dirty()) return;
    if (store_.setBlob(Bookmarks::kKey, marks_.encode())) marks_.markSaved();
  }

  Bookmarks &marks_;
  BlobStore &store_;
  const collection::CollectionSet &collections_;
  const playback::PlaybackStateMachine &playback_;
  uint32_t lastObserveMs_ = 0;
  uint32_t lastSaveMs_ = 0;
};

}  // namespace knobify::resume
